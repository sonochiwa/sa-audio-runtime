AudioJob BuildDialogueJob(
    const std::uint8_t* sound,
    std::uintptr_t sourceKey,
    std::uint32_t generation,
    std::int16_t bankId,
    AudioJobType type
) {
    AudioJob job{};
    job.type = type;
    job.sourceKey = sourceKey;
    job.sourceGeneration = generation;
    job.bankId = bankId;
    job.drySoundId = ReadSoundField<std::int16_t>(
        sound,
        kAeSoundIdOffset
    );
    job.defaultVolumeDb = ReadSoundField<float>(
        sound,
        kAeSoundVolumeOffset
    );
    job.baseRollOffFactor = std::max(
        ReadSoundField<float>(sound, kAeSoundRollOffOffset),
        0.01f
    );
    job.baseSpeed = ReadSoundField<float>(sound, kAeSoundSpeedOffset);
    if (type == AudioJobType::DialogueStart ||
        type == AudioJobType::StatefulStart) {
        const auto variance = ReadSoundField<float>(
            sound,
            kAeSoundSpeedVarianceOffset
        );
        if (variance > 0.0f && variance < job.baseSpeed) {
            job.baseSpeed += reinterpret_cast<RandomFloatFn>(
                kRandomFloatAddress
            )(-variance, variance);
        }
    }
    job.worldPosition = ReadSoundField<AudioVector>(
        sound,
        kAeSoundPositionOffset
    );
    job.relativePosition = PositionRelativeToCamera(job.worldPosition);
    const auto flags = ReadSoundField<std::uint16_t>(
        sound,
        kAeSoundFlagsOffset
    );
    job.isFrontEnd = (flags & kSoundFrontEnd) != 0;
    job.isUnpausable = (flags & kSoundUnpausable) != 0;
    job.startPercentage = (flags & kSoundStartPercentage) != 0;
    if (job.isFrontEnd) {
        job.relativePosition = job.worldPosition;
    }
    job.playTime = ReadSoundField<std::int16_t>(
        sound,
        kAeSoundPlayTimeOffset
    );
    job.effectsGainDb = ReadEffectsGainDb();
    return job;
}

void NotifyDialogueFinished(DialogueSoundProxy& proxy) {
    if (!proxy.active) {
        return;
    }
    proxy.active = false;
    const auto flags = ReadSoundField<std::uint16_t>(
        proxy.sound.data(),
        kAeSoundFlagsOffset
    );
    if ((flags & kSoundRequestUpdates) == 0) {
        return;
    }
    if (!proxy.owner) {
        return;
    }
    auto*** vtablePointer = reinterpret_cast<void***>(proxy.owner);
    if (!vtablePointer || !*vtablePointer || !(*vtablePointer)[0]) {
        return;
    }
    using UpdateParametersFn =
        void(__thiscall*)(void*, void*, std::int16_t);
    reinterpret_cast<UpdateParametersFn>((*vtablePointer)[0])(
        proxy.owner,
        proxy.sound.data(),
        -1
    );
}

void StopDialogue(DialogueSoundProxy& proxy) {
    if (!proxy.active) {
        return;
    }
    AudioJob job{};
    job.type = AudioJobType::DialogueStop;
    job.sourceKey = reinterpret_cast<std::uintptr_t>(proxy.sound.data());
    job.sourceGeneration = proxy.generation;
    DialogueBackendEnqueue(job);
}

void RemoveTerminatedDialogueProxy(void* owner) {
    const auto found = gDialogueSoundProxies.find(owner);
    if (found == gDialogueSoundProxies.end()) {
        return;
    }
    StopDialogue(found->second);
    found->second.active = false;
    gDialogueSoundProxies.erase(found);
}

struct DialogueProxyIdentity {
    void* owner{};
    std::uintptr_t sourceKey{};
    std::uint32_t generation{};
};

DialogueProxyIdentity GetDialogueProxyIdentity(
    void* owner,
    const DialogueSoundProxy& proxy
) {
    return {
        owner,
        reinterpret_cast<std::uintptr_t>(proxy.sound.data()),
        proxy.generation
    };
}

DialogueSoundProxyMap::iterator FindDialogueProxy(
    const DialogueProxyIdentity& identity
) {
    const auto proxy = gDialogueSoundProxies.find(identity.owner);
    if (proxy == gDialogueSoundProxies.end() ||
        reinterpret_cast<std::uintptr_t>(proxy->second.sound.data()) !=
            identity.sourceKey ||
        proxy->second.generation != identity.generation) {
        return gDialogueSoundProxies.end();
    }
    return proxy;
}

DialogueSoundProxyMap::iterator EraseDialogueProxy(
    const DialogueProxyIdentity& identity
) {
    const auto proxy = FindDialogueProxy(identity);
    return proxy != gDialogueSoundProxies.end()
        ? gDialogueSoundProxies.erase(proxy)
        : gDialogueSoundProxies.upper_bound(identity.owner);
}

void __fastcall HookPedSpeechTerminate(void* self, void*) {
    gOriginalPedSpeechTerminate(self);
    RemoveTerminatedDialogueProxy(self);
}

void __fastcall HookPedlessSpeechTerminate(void* self, void*) {
    gOriginalPedlessSpeechTerminate(self);
    RemoveTerminatedDialogueProxy(self);
}

void* __fastcall HookPoliceScannerDestructor(void* self, void*) {
    auto* result = gOriginalPoliceScannerDestructor(self);
    RemoveTerminatedDialogueProxy(self);
    return result;
}

void ServiceDialogueProxies() {
    AudioCompletion completion{};
    while (DialogueBackendPollCompletion(completion)) {
        if (HandleVehicleCompletion(completion)) {
            continue;
        }
        bool handled{};
        for (auto proxy = gDialogueSoundProxies.begin();
             proxy != gDialogueSoundProxies.end();
             ++proxy) {
            auto& value = proxy->second;
            if (reinterpret_cast<std::uintptr_t>(value.sound.data()) !=
                    completion.sourceKey ||
                value.generation != completion.sourceGeneration) {
                continue;
            }
            if (!completion.finished) {
                std::memcpy(
                    value.sound.data() + kAeSoundLengthOffset,
                    &completion.lengthMs,
                    sizeof(completion.lengthMs)
                );
                handled = true;
                break;
            }
            const auto identity =
                GetDialogueProxyIdentity(proxy->first, value);
            NotifyDialogueFinished(value);
            EraseDialogueProxy(identity);
            handled = true;
            break;
        }
        if (!handled) {
            HandleStatefulCompletion(completion);
        }
    }

    if (!DialogueBackendShouldReplaceOriginal()) {
        while (!gDialogueSoundProxies.empty()) {
            const auto owner = gDialogueSoundProxies.begin()->first;
            auto& proxy = gDialogueSoundProxies.begin()->second;
            const auto identity = GetDialogueProxyIdentity(owner, proxy);
            StopDialogue(proxy);
            NotifyDialogueFinished(proxy);
            EraseDialogueProxy(identity);
        }
        return;
    }

    for (auto proxy = gDialogueSoundProxies.begin();
         proxy != gDialogueSoundProxies.end();) {
        auto& value = proxy->second;
        const bool moduleEnabled = AudioConfigIsEnabled() &&
            (value.isScanner
                ? AudioConfigScannerEnabled()
                : AudioConfigDialoguesEnabled());
        if (!moduleEnabled) {
            const auto identity =
                GetDialogueProxyIdentity(proxy->first, value);
            StopDialogue(value);
            NotifyDialogueFinished(value);
            proxy = EraseDialogueProxy(identity);
            continue;
        }
        const auto stopRequested = ReadSoundField<std::int16_t>(
            value.sound.data(),
            kAeSoundStopRequestedOffset
        ) != 0;
        if (stopRequested || !value.owner) {
            const auto identity =
                GetDialogueProxyIdentity(proxy->first, value);
            StopDialogue(value);
            NotifyDialogueFinished(value);
            proxy = EraseDialogueProxy(identity);
            continue;
        }
        const auto flags = ReadSoundField<std::uint16_t>(
            value.sound.data(),
            kAeSoundFlagsOffset
        );
        if ((flags & kSoundRequestUpdates) == 0) {
            proxy = gDialogueSoundProxies.erase(proxy);
            continue;
        }

        auto*** vtablePointer = reinterpret_cast<void***>(value.owner);
        if (!vtablePointer || !*vtablePointer || !(*vtablePointer)[0]) {
            StopDialogue(value);
            proxy = gDialogueSoundProxies.erase(proxy);
            continue;
        }
        using UpdateParametersFn =
            void(__thiscall*)(void*, void*, std::int16_t);
        const auto now = *reinterpret_cast<const std::uint32_t*>(
            kGameTimeMsAddress
        );
        if (value.lastPlayTimeMs != 0) {
            value.playPositionMs +=
                static_cast<float>(now - value.lastPlayTimeMs) *
                value.resolvedSpeed;
        }
        value.lastPlayTimeMs = now;
        const auto playPosition = static_cast<std::int16_t>(std::clamp(
            value.playPositionMs,
            0.0f,
            32767.0f
        ));
        const auto identity =
            GetDialogueProxyIdentity(proxy->first, value);
        reinterpret_cast<UpdateParametersFn>((*vtablePointer)[0])(
            value.owner,
            value.sound.data(),
            playPosition
        );

        proxy = FindDialogueProxy(identity);
        if (proxy == gDialogueSoundProxies.end()) {
            proxy = gDialogueSoundProxies.upper_bound(identity.owner);
            continue;
        }
        auto& updatedValue = proxy->second;
        if (ReadSoundField<std::int16_t>(
                updatedValue.sound.data(),
                kAeSoundStopRequestedOffset
            ) != 0) {
            StopDialogue(updatedValue);
            NotifyDialogueFinished(updatedValue);
            proxy = EraseDialogueProxy(identity);
            continue;
        }
        if ((ReadSoundField<std::uint16_t>(
                 updatedValue.sound.data(),
                 kAeSoundFlagsOffset
             ) & kSoundRequestUpdates) == 0) {
            proxy = gDialogueSoundProxies.erase(proxy);
            continue;
        }
        auto job = BuildDialogueJob(
            updatedValue.sound.data(),
            identity.sourceKey,
            updatedValue.generation,
            updatedValue.bankId,
            AudioJobType::DialogueUpdate
        );
        job.baseSpeed = updatedValue.resolvedSpeed;
        DialogueBackendEnqueue(job);
        ++proxy;
    }
}

void BeginVehicleCapture(
    void* owner,
    std::int32_t soundType,
    std::int16_t bankId
) {
    gVehicleCapture = {owner, soundType, bankId};
}

void EndVehicleCapture() {
    gVehicleCapture = {};
    gVehicleCapture.bankId = -1;
}

bool IsDialogueRendererEnabled() {
    return AudioConfigIsEnabled() && AudioConfigDialoguesEnabled();
}

bool IsScannerRendererEnabled() {
    return AudioConfigIsEnabled() && AudioConfigScannerEnabled();
}

bool IsEffectsRendererEnabled() {
    return AudioConfigIsEnabled() &&
           AudioConfigMiscEffectsEnabled();
}

bool IsAudioRuntimePaused() {
    constexpr std::uintptr_t kCodePauseAddress = 0xB7CB48;
    constexpr std::uintptr_t kUserPauseAddress = 0xB7CB49;
    return *reinterpret_cast<const volatile std::uint8_t*>(
               kCodePauseAddress
           ) != 0 ||
           *reinterpret_cast<const volatile std::uint8_t*>(
               kUserPauseAddress
           ) != 0;
}

bool ResolveLoadedBank(std::int16_t bankSlot, std::int16_t& bankId) {
    if (bankSlot < 0) {
        return false;
    }
    __try {
        const auto* loader = *reinterpret_cast<const std::uint8_t* const*>(
            kAudioHardwareAddress + kHardwareBankLoaderOffset
        );
        if (!loader) {
            return false;
        }
        const auto slotCount = *reinterpret_cast<const std::uint16_t*>(
            loader + kBankLoaderSlotCountOffset
        );
        const auto* slots = *reinterpret_cast<const std::uint8_t* const*>(
            loader
        );
        if (!slots || bankSlot >= static_cast<std::int16_t>(slotCount)) {
            return false;
        }
        bankId = *reinterpret_cast<const std::int16_t*>(
            slots +
            static_cast<std::size_t>(bankSlot) * kBankSlotSize +
            kBankSlotBankIdOffset
        );
        return bankId >= 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsStatefulModuleEnabled(StatefulSoundModule module) {
    if (!AudioConfigIsEnabled()) {
        return false;
    }
    switch (module) {
    case StatefulSoundModule::Explosions:
        return AudioConfigExplosionsEnabled();
    case StatefulSoundModule::WeaponEffects:
        return AudioConfigWeaponEffectsEnabled();
    case StatefulSoundModule::VehicleCollisions:
        return AudioConfigVehicleCollisionsEnabled();
    case StatefulSoundModule::Characters:
        return AudioConfigCharacterEffectsEnabled();
    case StatefulSoundModule::WorldAmbience:
        return AudioConfigWorldAmbienceEnabled();
    }
    return false;
}

bool ClassifyStatefulSound(
    void* owner,
    std::int16_t bankSlot,
    std::int16_t bankId,
    std::int16_t soundId,
    std::int32_t eventId,
    StatefulSoundModule& module
) {
    if (!owner) {
        return false;
    }

    std::uintptr_t vtable{};
    std::uintptr_t updateParameters{};
    __try {
        vtable = *reinterpret_cast<const std::uintptr_t*>(owner);
        updateParameters =
            *reinterpret_cast<const std::uintptr_t*>(vtable);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    constexpr std::uintptr_t kExplosionVtable = 0x862E60;
    constexpr std::uintptr_t kCollisionVtable = 0x862E64;
    constexpr std::uintptr_t kDoorVtable = 0x85998C;
    constexpr std::uintptr_t kFireVtable = 0x85AA94;
    constexpr std::uintptr_t kWeatherVtable = 0x872A74;
    constexpr std::uintptr_t kGlobalWeaponVtable = 0x862E5C;
    constexpr std::uintptr_t kWaterCannonVtable = 0x872A60;
    constexpr std::uintptr_t kScriptVtable = 0x862E58;
    constexpr std::uintptr_t kPedUpdateParameters = 0x4E1180;
    constexpr std::uintptr_t kWeaponUpdateParameters = 0x504B70;

    if (updateParameters == kPedUpdateParameters ||
        (bankId == kWeaponBankId &&
         (soundId == 19 || soundId == 20))) {
        module = StatefulSoundModule::Characters;
    } else if (vtable == kExplosionVtable) {
        module = StatefulSoundModule::Explosions;
    } else if (vtable == kCollisionVtable || vtable == kDoorVtable) {
        module = StatefulSoundModule::VehicleCollisions;
    } else if (vtable == kFireVtable || vtable == kWeatherVtable) {
        module = StatefulSoundModule::WorldAmbience;
    } else if (vtable == kWaterCannonVtable ||
               vtable == kScriptVtable ||
               (vtable == kGlobalWeaponVtable &&
                (bankId == kCollisionBankId ||
                 bankId == kVehicleHornBankId))) {
        module = StatefulSoundModule::WorldAmbience;
    } else if (vtable == kGlobalWeaponVtable ||
               updateParameters == kWeaponUpdateParameters ||
               bankId == kWeaponBankId ||
               bankId == 36 ||
               (bankId == kVehicleGeneralBankId &&
                eventId >= 1 && eventId <= 14)) {
        module = StatefulSoundModule::WeaponEffects;
    } else if (bankId == 52) {
        module = StatefulSoundModule::Explosions;
    } else if (bankId == kCollisionBankId) {
        module = StatefulSoundModule::Characters;
    } else if ((bankId >= 0 && bankId <= 6) ||
               bankId == 128 ||
               (bankId >= 144 && bankId <= 146) ||
               bankSlot == 30 ||
               bankSlot == 32 ||
               bankSlot == 41) {
        module = StatefulSoundModule::Characters;
    } else if (bankId == kRainBankId) {
        module = StatefulSoundModule::WorldAmbience;
    } else {
        return false;
    }
    return true;
}

void StopStatefulSound(StatefulSoundProxy& proxy) {
    if (!proxy.active || !proxy.started) {
        return;
    }
    AudioJob job{};
    job.type = AudioJobType::StatefulStop;
    job.sourceKey = reinterpret_cast<std::uintptr_t>(proxy.sound.data());
    job.sourceGeneration = proxy.generation;
    DialogueBackendEnqueue(job);
}

void FinishStatefulSound(StatefulSoundProxy& proxy) {
    if (!proxy.active) {
        return;
    }
    proxy.active = false;
    const auto flags = ReadSoundField<std::uint16_t>(
        proxy.sound.data(),
        kAeSoundFlagsOffset
    );
    if ((flags & kSoundRequestUpdates) != 0 && proxy.owner) {
        reinterpret_cast<void(__thiscall*)(void*, std::int16_t)>(
            kUpdateSoundParametersAddress
        )(proxy.sound.data(), -1);
    }
    reinterpret_cast<void(__thiscall*)(void*)>(
        kUnregisterSoundAddress
    )(proxy.sound.data());
}

bool HandleStatefulCompletion(const AudioCompletion& completion) {
    for (auto proxy = gStatefulSoundProxies.begin();
         proxy != gStatefulSoundProxies.end();
         ++proxy) {
        auto& value = proxy->second;
        if (reinterpret_cast<std::uintptr_t>(value.sound.data()) !=
                completion.sourceKey ||
            value.generation != completion.sourceGeneration) {
            continue;
        }
        if (!completion.finished) {
            std::memcpy(
                value.sound.data() + kAeSoundLengthOffset,
                &completion.lengthMs,
                sizeof(completion.lengthMs)
            );
            return true;
        }
        FinishStatefulSound(value);
        gStatefulSoundProxies.erase(proxy);
        return true;
    }
    return false;
}

void ServiceStatefulSounds() {
    for (auto proxy = gStatefulSoundProxies.begin();
         proxy != gStatefulSoundProxies.end();) {
        auto& value = proxy->second;
        if (!value.active || !IsStatefulModuleEnabled(value.module)) {
            StopStatefulSound(value);
            FinishStatefulSound(value);
            proxy = gStatefulSoundProxies.erase(proxy);
            continue;
        }

        const auto now = *reinterpret_cast<const std::uint32_t*>(
            kGameTimeMsAddress
        );
        const auto flags = ReadSoundField<std::uint16_t>(
            value.sound.data(),
            kAeSoundFlagsOffset
        );
        if (IsAudioRuntimePaused() &&
            (flags & kSoundUnpausable) == 0) {
            value.lastPlayTimeMs = now;
            ++proxy;
            continue;
        }
        if (ReadSoundField<std::int16_t>(
                value.sound.data(),
                kAeSoundStopRequestedOffset
            ) != 0) {
            StopStatefulSound(value);
            FinishStatefulSound(value);
            proxy = gStatefulSoundProxies.erase(proxy);
            continue;
        }
        if (!value.started && value.remainingFrameDelay > 0) {
            --value.remainingFrameDelay;
            std::memcpy(
                value.sound.data() + kAeSoundFrameDelayOffset,
                &value.remainingFrameDelay,
                sizeof(value.remainingFrameDelay)
            );
            value.lastPlayTimeMs = now;
            if (value.remainingFrameDelay > 0) {
                ++proxy;
                continue;
            }
            auto startJob = BuildDialogueJob(
                value.sound.data(),
                reinterpret_cast<std::uintptr_t>(value.sound.data()),
                value.generation,
                value.bankId,
                AudioJobType::StatefulStart
            );
            startJob.baseSpeed = value.resolvedSpeed;
            if (!DialogueBackendEnqueue(startJob)) {
                FinishStatefulSound(value);
                proxy = gStatefulSoundProxies.erase(proxy);
                continue;
            }
            value.started = true;
        }
        if (value.lastPlayTimeMs != 0) {
            value.playPositionMs +=
                static_cast<float>(now - value.lastPlayTimeMs) *
                std::max(value.resolvedSpeed, 0.05f);
        }
        value.lastPlayTimeMs = now;
        const auto playPosition = static_cast<std::int16_t>(std::clamp(
            value.playPositionMs,
            0.0f,
            32767.0f
        ));
        const auto proxyKey = proxy->first;

        reinterpret_cast<void(__thiscall*)(void*, std::int16_t)>(
            kUpdateSoundParametersAddress
        )(value.sound.data(), playPosition);

        proxy = gStatefulSoundProxies.find(proxyKey);
        if (proxy == gStatefulSoundProxies.end()) {
            proxy = gStatefulSoundProxies.upper_bound(proxyKey);
            continue;
        }
        auto& updatedValue = proxy->second;
        if (ReadSoundField<std::int16_t>(
                updatedValue.sound.data(),
                kAeSoundStopRequestedOffset
            ) != 0) {
            StopStatefulSound(updatedValue);
            FinishStatefulSound(updatedValue);
            proxy = gStatefulSoundProxies.erase(proxy);
            continue;
        }

        auto job = BuildDialogueJob(
            updatedValue.sound.data(),
            reinterpret_cast<std::uintptr_t>(
                updatedValue.sound.data()
            ),
            updatedValue.generation,
            updatedValue.bankId,
            AudioJobType::StatefulUpdate
        );
        const auto listenerSpeed = ReadSoundField<float>(
            updatedValue.sound.data(),
            kAeSoundListenerSpeedOffset
        );
        const auto relativeFrequency =
            reinterpret_cast<float(__thiscall*)(void*)>(
                kGetRelativeFrequencyAddress
            )(updatedValue.sound.data());
        job.baseSpeed = std::max(relativeFrequency, 0.05f);
        updatedValue.resolvedSpeed =
            listenerSpeed > 0.0f ? listenerSpeed : job.baseSpeed;
        DialogueBackendEnqueue(job);
        ++proxy;
    }
}

bool StatefulSoundMatches(
    const StatefulSoundProxy& proxy,
    void* owner,
    std::int32_t eventId,
    void* physicalEntity,
    std::int16_t bankSlot
) {
    if (!proxy.active) {
        return false;
    }
    if (owner && proxy.owner != owner) {
        return false;
    }
    if (eventId != -1 &&
        ReadSoundField<std::int32_t>(
            proxy.sound.data(),
            0x0C
        ) != eventId) {
        return false;
    }
    if (physicalEntity &&
        ReadSoundField<void*>(
            proxy.sound.data(),
            kAeSoundPhysicalEntityOffset
        ) != physicalEntity) {
        return false;
    }
    if (bankSlot != -1 &&
        ReadSoundField<std::int16_t>(
            proxy.sound.data(),
            kAeSoundBankSlotOffset
        ) != bankSlot) {
        return false;
    }
    return true;
}

bool HasStatefulSound(
    void* owner,
    std::int16_t eventId,
    void* physicalEntity
) {
    return std::any_of(
        gStatefulSoundProxies.begin(),
        gStatefulSoundProxies.end(),
        [=](const auto& entry) {
            return StatefulSoundMatches(
                entry.second,
                owner,
                eventId,
                physicalEntity,
                -1
            );
        }
    );
}

bool HasStatefulSoundInBank(std::int16_t bankSlot) {
    return std::any_of(
        gStatefulSoundProxies.begin(),
        gStatefulSoundProxies.end(),
        [=](const auto& entry) {
            return StatefulSoundMatches(
                entry.second,
                nullptr,
                -1,
                nullptr,
                bankSlot
            );
        }
    );
}

void CancelStatefulSounds(
    void* owner,
    std::int32_t eventId,
    void* physicalEntity,
    std::int16_t bankSlot,
    bool forget
) {
    for (auto proxy = gStatefulSoundProxies.begin();
         proxy != gStatefulSoundProxies.end();) {
        auto& value = proxy->second;
        if (!StatefulSoundMatches(
                value,
                owner,
                eventId,
                physicalEntity,
                bankSlot
            )) {
            ++proxy;
            continue;
        }
        if (forget) {
            auto flags = ReadSoundField<std::uint16_t>(
                value.sound.data(),
                kAeSoundFlagsOffset
            );
            flags &= static_cast<std::uint16_t>(~kSoundRequestUpdates);
            std::memcpy(
                value.sound.data() + kAeSoundFlagsOffset,
                &flags,
                sizeof(flags)
            );
            value.owner = nullptr;
            void* noOwner{};
            std::memcpy(
                value.sound.data() + 4,
                &noOwner,
                sizeof(noOwner)
            );
        } else {
            reinterpret_cast<void(__thiscall*)(void*)>(
                kStopSoundAddress
            )(value.sound.data());
            ++proxy;
            continue;
        }
        StopStatefulSound(value);
        FinishStatefulSound(value);
        proxy = gStatefulSoundProxies.erase(proxy);
    }
}

std::int16_t __fastcall HookAreEventSoundsPlaying(
    void* self,
    void*,
    std::int16_t eventId,
    void* owner
) {
    const auto original =
        gOriginalAreEventSoundsPlaying(self, eventId, owner);
    if (original != 0) {
        return original;
    }
    return HasStatefulSound(owner, eventId, nullptr) ? 2 : 0;
}

std::int16_t __fastcall HookAreBankSoundsPlaying(
    void* self,
    void*,
    std::int16_t bankSlot
) {
    const auto original =
        gOriginalAreBankSoundsPlaying(self, bankSlot);
    if (original != 0) {
        return original;
    }
    return HasStatefulSoundInBank(bankSlot) ? 2 : 0;
}

std::int16_t __fastcall HookAreEventPhysicalSoundsPlaying(
    void* self,
    void*,
    std::int16_t eventId,
    void* owner,
    void* physicalEntity
) {
    const auto original = gOriginalAreEventPhysicalSoundsPlaying(
        self,
        eventId,
        owner,
        physicalEntity
    );
    if (original != 0) {
        return original;
    }
    return HasStatefulSound(owner, eventId, physicalEntity) ? 2 : 0;
}

void __fastcall HookCancelEventSounds(
    void* self,
    void*,
    std::int16_t eventId,
    void* owner
) {
    CancelStatefulSounds(owner, eventId, nullptr, -1, true);
    gOriginalCancelEventSounds(self, eventId, owner);
}

void __fastcall HookCancelEventPhysicalSounds(
    void* self,
    void*,
    std::int16_t eventId,
    void* owner,
    void* physicalEntity
) {
    CancelStatefulSounds(
        owner,
        eventId,
        physicalEntity,
        -1,
        true
    );
    gOriginalCancelEventPhysicalSounds(
        self,
        eventId,
        owner,
        physicalEntity
    );
}

void __fastcall HookCancelBankSlotSounds(
    void* self,
    void*,
    std::int16_t bankSlot,
    bool fullStop
) {
    CancelStatefulSounds(nullptr, -1, nullptr, bankSlot, fullStop);
    gOriginalCancelBankSlotSounds(self, bankSlot, fullStop);
}

void __fastcall HookCancelOwnedSounds(
    void* self,
    void*,
    void* owner,
    bool fullStop
) {
    CancelStatefulSounds(owner, -1, nullptr, -1, fullStop);
    gOriginalCancelOwnedSounds(self, owner, fullStop);
}

void* __fastcall HookRequestNewSound(
    void* self,
    void*,
    void* sound
) {
    VehicleCapture capture = gVehicleCapture;
    if (!sound) {
        return gOriginalRequestNewSound(self, sound);
    }
    if (IsAudioRuntimePaused()) {
        return gOriginalRequestNewSound(self, sound);
    }
    if (!capture.owner) {
        const auto* bytes = static_cast<const std::uint8_t*>(sound);
        auto* audioEntity = *reinterpret_cast<void* const*>(bytes + 4);
        const auto bankSlot = *reinterpret_cast<const std::int16_t*>(
            bytes + kAeSoundBankSlotOffset
        );
        const bool isSpeech =
            bankSlot >= kSpeechBankSlotFirst &&
            bankSlot <= kSpeechBankSlotLast &&
            IsDialogueRendererEnabled();
        const bool isScanner =
            bankSlot >= kScannerBankSlotFirst &&
            bankSlot <= kScannerBankSlotLast &&
            IsScannerRendererEnabled();
        if (DialogueBackendShouldReplaceOriginal() &&
            audioEntity && (isSpeech || isScanner)) {
            std::int16_t bankId{-1};
            if (isSpeech) {
                bankId = *reinterpret_cast<const std::int16_t*>(
                    static_cast<const std::uint8_t*>(audioEntity) +
                    kSpeechBankIdOffset
                );
            } else {
                const auto* slots = *reinterpret_cast<
                    const std::uint8_t* const*
                >(kScannerCurrentSlotsAddress);
                if (slots) {
                    bankId = *reinterpret_cast<const std::int16_t*>(
                        slots +
                        (bankSlot - kScannerBankSlotFirst) * 4
                    );
                }
            }
            if (bankId >= 0) {
                auto [entry, inserted] =
                    gDialogueSoundProxies.try_emplace(audioEntity);
                auto& proxy = entry->second;
                const auto generation = proxy.generation + 1 == 0
                    ? 1u
                    : proxy.generation + 1;
                if (proxy.active) {
                    StopDialogue(proxy);
                }
                auto job = BuildDialogueJob(
                    bytes,
                    reinterpret_cast<std::uintptr_t>(proxy.sound.data()),
                    generation,
                    bankId,
                    AudioJobType::DialogueStart
                );
                if (DialogueBackendEnqueue(job)) {
                    std::memcpy(
                        proxy.sound.data(),
                        sound,
                        proxy.sound.size()
                    );
                    proxy.owner = audioEntity;
                    proxy.bankId = bankId;
                    proxy.generation = generation;
                    proxy.resolvedSpeed = job.baseSpeed;
                    proxy.playPositionMs = 0.0f;
                    proxy.lastPlayTimeMs =
                        *reinterpret_cast<const std::uint32_t*>(
                            kGameTimeMsAddress
                        );
                    proxy.isScanner = isScanner;
                    proxy.active = true;
                    reinterpret_cast<void(__thiscall*)(void*)>(
                        kUnregisterSoundAddress
                    )(sound);
                    void* noPhysicalEntity{};
                    std::memcpy(
                        proxy.sound.data() + 8,
                        &noPhysicalEntity,
                        sizeof(noPhysicalEntity)
                    );
                    return proxy.sound.data();
                }
                if (inserted) {
                    gDialogueSoundProxies.erase(entry);
                }
            }
        }
        const auto flags = *reinterpret_cast<const std::uint16_t*>(
            bytes + kAeSoundFlagsOffset
        );
        const auto eventId = *reinterpret_cast<const std::int32_t*>(
            bytes + 0x0C
        );
        const auto soundId = *reinterpret_cast<const std::int16_t*>(
            bytes + kAeSoundIdOffset
        );
        const bool eventNeedsOriginalTracking =
            eventId == 102 ||
            eventId == 107 ||
            (eventId >= 113 && eventId <= 115) ||
            eventId == 119;
        if (IsVehicleEffectRendererEnabled() &&
            gVehicleAudioOwners.find(audioEntity) !=
                gVehicleAudioOwners.end() &&
            (flags & (kSoundRequestUpdates |
                      kSoundLifespanTiedToEntity)) == 0 &&
            !eventNeedsOriginalTracking) {
            AudioJob job{};
            job.type = AudioJobType::VehicleOneShot;
            job.sourceKey =
                (static_cast<std::uintptr_t>(++gVehicleOneShotSequence) << 1) |
                1u;
            job.bankId = ResolvePersistentVehicleBank(
                audioEntity,
                bytes,
                -1
            );
            job.drySoundId = *reinterpret_cast<const std::int16_t*>(
                bytes + kAeSoundIdOffset
            );
            job.defaultVolumeDb = *reinterpret_cast<const float*>(
                bytes + kAeSoundVolumeOffset
            );
            job.baseRollOffFactor = *reinterpret_cast<const float*>(
                bytes + kAeSoundRollOffOffset
            );
            const auto speed = *reinterpret_cast<const float*>(
                bytes + kAeSoundSpeedOffset
            );
            const auto variance = *reinterpret_cast<const float*>(
                bytes + kAeSoundSpeedVarianceOffset
            );
            job.baseSpeed =
                variance > 0.0f && variance < speed
                    ? speed + reinterpret_cast<RandomFloatFn>(
                          kRandomFloatAddress
                      )(-variance, variance)
                    : speed;
            job.worldPosition = *reinterpret_cast<const AudioVector*>(
                bytes + kAeSoundPositionOffset
            );
            job.relativePosition = PositionRelativeToCamera(
                job.worldPosition
            );
            job.startPercentage =
                (flags & kSoundStartPercentage) != 0;
            job.playTime = *reinterpret_cast<const std::int16_t*>(
                bytes + kAeSoundPlayTimeOffset
            );
            job.effectsGainDb = ReadEffectsGainDb();
            if (job.bankId >= 0 && VehicleBackendEnqueue(job)) {
                return nullptr;
            }
        }
        const auto frameDelay = *reinterpret_cast<const std::uint8_t*>(
            bytes + kAeSoundFrameDelayOffset
        );
        const bool isSpeechSlot =
            bankSlot >= kSpeechBankSlotFirst &&
            bankSlot <= kSpeechBankSlotLast;
        const bool isScannerSlot =
            bankSlot >= kScannerBankSlotFirst &&
            bankSlot <= kScannerBankSlotLast;
        const bool belongsToVehicle =
            gVehicleAudioOwners.find(audioEntity) !=
            gVehicleAudioOwners.end();
        if (DialogueBackendShouldReplaceOriginal() &&
            !isSpeechSlot &&
            !isScannerSlot &&
            !belongsToVehicle &&
            (flags & kSoundMusicMastered) == 0 &&
            ((flags & (kSoundRequestUpdates |
                       kSoundLifespanTiedToEntity)) != 0 ||
             frameDelay > 0) &&
            gStatefulSoundProxies.size() < 128) {
            std::int16_t bankId{-1};
            StatefulSoundModule module{};
            if (ResolveLoadedBank(bankSlot, bankId) &&
                ClassifyStatefulSound(
                    audioEntity,
                    bankSlot,
                    bankId,
                    soundId,
                    eventId,
                    module
                ) &&
                IsStatefulModuleEnabled(module)) {
                do {
                    ++gStatefulSoundSequence;
                } while (gStatefulSoundSequence == 0 ||
                         gStatefulSoundProxies.find(
                             gStatefulSoundSequence
                         ) != gStatefulSoundProxies.end());
                auto [entry, inserted] =
                    gStatefulSoundProxies.try_emplace(
                        gStatefulSoundSequence
                    );
                auto& proxy = entry->second;
                std::memcpy(
                    proxy.sound.data(),
                    sound,
                    proxy.sound.size()
                );
                proxy.owner = audioEntity;
                proxy.bankId = bankId;
                proxy.generation = 1;
                proxy.module = module;
                proxy.playPositionMs = 0.0f;
                proxy.remainingFrameDelay = frameDelay;
                proxy.started = frameDelay == 0;
                proxy.lastPlayTimeMs =
                    *reinterpret_cast<const std::uint32_t*>(
                        kGameTimeMsAddress
                    );
                auto job = BuildDialogueJob(
                    proxy.sound.data(),
                    reinterpret_cast<std::uintptr_t>(
                        proxy.sound.data()
                    ),
                    proxy.generation,
                    bankId,
                    AudioJobType::StatefulStart
                );
                proxy.resolvedSpeed = job.baseSpeed;
                std::memcpy(
                    proxy.sound.data() + kAeSoundListenerSpeedOffset,
                    &proxy.resolvedSpeed,
                    sizeof(proxy.resolvedSpeed)
                );
                if (!proxy.started || DialogueBackendEnqueue(job)) {
                    proxy.active = true;
                    auto* physicalEntity =
                        *reinterpret_cast<void* const*>(
                            proxy.sound.data() +
                            kAeSoundPhysicalEntityOffset
                        );
                    reinterpret_cast<void(__thiscall*)(void*)>(
                        kUnregisterSoundAddress
                    )(sound);
                    if (physicalEntity) {
                        reinterpret_cast<
                            void(__thiscall*)(void*, void*)
                        >(kRegisterSoundAddress)(
                            proxy.sound.data(),
                            physicalEntity
                        );
                    }
                    return proxy.sound.data();
                }
                if (inserted) {
                    gStatefulSoundProxies.erase(entry);
                }
            }
        }
        if (DialogueBackendShouldReplaceOriginal() &&
            !isSpeechSlot &&
            !isScannerSlot &&
            !belongsToVehicle &&
            (flags & (kSoundRequestUpdates |
                      kSoundLifespanTiedToEntity |
                      kSoundMusicMastered)) == 0 &&
            frameDelay == 0) {
            std::int16_t bankId{-1};
            if (ResolveLoadedBank(bankSlot, bankId)) {
                StatefulSoundModule module{};
                const bool categorized = ClassifyStatefulSound(
                    audioEntity,
                    bankSlot,
                    bankId,
                    soundId,
                    eventId,
                    module
                );
                if ((categorized &&
                     !IsStatefulModuleEnabled(module)) ||
                    (!categorized &&
                     !IsEffectsRendererEnabled())) {
                    return gOriginalRequestNewSound(self, sound);
                }
                auto job = BuildDialogueJob(
                    bytes,
                    (static_cast<std::uintptr_t>(
                         ++gVehicleOneShotSequence
                     ) << 1) | 1u,
                    1,
                    bankId,
                    AudioJobType::GenericOneShot
                );
                if (DialogueBackendEnqueue(job)) {
                    return nullptr;
                }
            }
        }
        return gOriginalRequestNewSound(self, sound);
    }
    if (capture.soundType < 0 ||
        capture.soundType >=
            static_cast<std::int32_t>(kVehicleEngineSoundCount)) {
        return gOriginalRequestNewSound(self, sound);
    }

    auto& proxy = gVehicleSoundProxies[
        GetVehicleProxyKey(
            capture.owner,
            capture.soundType
        )
    ];
    std::memcpy(proxy.sound.data(), sound, proxy.sound.size());
    proxy.owner = capture.owner;
    proxy.soundType = capture.soundType;
    proxy.bankId = capture.bankId;
    ++proxy.generation;
    if (proxy.generation == 0) {
        ++proxy.generation;
    }
    proxy.playPositionMs = 0.0f;
    proxy.lastCursorTimeMs =
        *reinterpret_cast<const std::uint32_t*>(kGameTimeMsAddress);
    if (auto* entity = *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(proxy.owner) + 4
        )) {
        proxy.previousDistance = VectorMagnitude(
            PositionRelativeToCamera(ReadPlaceablePosition(entity))
        );
        proxy.previousPositionTimeMs = proxy.lastCursorTimeMs;
    } else {
        proxy.previousDistance = 0.0f;
        proxy.previousPositionTimeMs = 0;
    }
    const auto flags = ReadProxyField<std::uint16_t>(
        proxy,
        kAeSoundFlagsOffset
    );
    proxy.tracksAccelerationCursor =
        proxy.soundType == 4 &&
        (flags & kSoundStartPercentage) != 0;
    proxy.active = true;
    if (proxy.tracksAccelerationCursor) {
        const std::int16_t accelerationLength = 1000;
        std::memcpy(
            proxy.sound.data() + kAeSoundLengthOffset,
            &accelerationLength,
            sizeof(accelerationLength)
        );
        const auto playTime = std::clamp(
            ReadProxyField<std::int16_t>(
                proxy,
                kAeSoundPlayTimeOffset
            ),
            static_cast<std::int16_t>(0),
            static_cast<std::int16_t>(100)
        );
        proxy.playPositionMs =
            static_cast<float>(playTime) * 10.0f;
    }
    return proxy.sound.data();
}

