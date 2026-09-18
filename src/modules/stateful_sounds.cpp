#include "modules/modules.h"

namespace runtime {

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

} // namespace runtime
