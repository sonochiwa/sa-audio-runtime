#include "modules/modules.h"

namespace runtime {

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

} // namespace runtime
