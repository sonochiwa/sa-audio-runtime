#include "modules/modules.h"

namespace runtime {

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
            (flags & (kSoundCancellable |
                      kSoundRequestUpdates |
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
            (flags & (kSoundCancellable |
                      kSoundRequestUpdates |
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

} // namespace runtime
