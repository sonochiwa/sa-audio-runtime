#include "backend/backend.h"

namespace backend {

DWORD WINAPI BackendThread(void*) {
    auto weaponBankStorage = std::make_unique<OriginalSoundBank>();
    auto bulletHitBankStorage = std::make_unique<OriginalSoundBank>();
    auto& weaponBank = *weaponBankStorage;
    auto& bulletHitBank = *bulletHitBankStorage;
    std::string error;
    const auto gameDirectory = GetGameDirectory();
    std::string activeArchivePath = gameDirectory + "\\audio\\SFX\\GENRL";
    std::string activeLookupPath =
        gameDirectory + "\\audio\\CONFIG\\BankLkup.dat";
    if (!weaponBank.Load(gameDirectory, kWeaponBankId, error) ||
        !bulletHitBank.Load(gameDirectory, kBulletHitBankId, error)) {
        return 1;
    }

    IDirectSound8* directSound{};
    IDirectSound3DListener* listener{};
    // The device needs the game window, which a large ModLoader setup can
    // delay well past the first seconds; the worker keeps trying, at a lower
    // rate after the first five seconds, until the game exits.
    for (int attempt = 0;
         !CreateAudioDevice(&directSound, &listener);
         ++attempt) {
        const DWORD retryMs = attempt < 100 ? 50 : 500;
        if (WaitForSingleObject(gStopEvent, retryMs) == WAIT_OBJECT_0) {
            return 2;
        }
    }
    if (!directSound || !listener) {
        return 3;
    }

    std::array<IDirectSoundBuffer*, kMaxOriginalSounds> weaponBaseBuffers{};
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds> bulletHitBaseBuffers{};
    std::vector<Voice> voices;
    std::vector<MinigunSource> minigunSources;
    std::vector<AudioJob> coalescedJobs;
    std::unordered_map<std::uintptr_t, VehicleSource> vehicleSources;
    std::unordered_map<std::uintptr_t, VirtualRuntimeSource>
        virtualRuntimeSources;
    std::unordered_map<
        std::int16_t,
        std::unique_ptr<VehicleBank>
    > vehicleBanks;
    voices.reserve(128);
    minigunSources.reserve(16);
    coalescedJobs.reserve(1024);
    vehicleSources.reserve(256);
    virtualRuntimeSources.reserve(128);
    vehicleBanks.reserve(64);
    gReady.store(true, std::memory_order_release);

    HANDLE waits[] = {gStopEvent, gWakeEvent};
    bool wasPaused{};
    bool replacementWasEnabled =
        gReplaceOriginal.load(std::memory_order_acquire);
    bool vehicleReplacementWasEnabled =
        gReplaceVehicles.load(std::memory_order_acquire);
    bool dialogueReplacementWasEnabled =
        gReplaceDialogues.load(std::memory_order_acquire);
    auto resetEpoch = gResetEpoch.load(std::memory_order_acquire);
    auto nextDeviceHealthCheck = GetTickCount64() + 1000;
    while (WaitForMultipleObjects(2, waits, FALSE, 10) != WAIT_OBJECT_0) {
        // Cleared before the queues are read: a job queued after this point
        // signals again, one queued before it is seen by this pass.
        gWakePending.exchange(false, std::memory_order_acq_rel);
        PublishDebugSnapshot(
            voices,
            vehicleSources.size(),
            minigunSources.size(),
            virtualRuntimeSources.size()
        );
        const auto requestedReset =
            gResetEpoch.load(std::memory_order_acquire);
        if (requestedReset != resetEpoch) {
            resetEpoch = requestedReset;
            StopAndReleaseVoices(voices, false);
            minigunSources.clear();
            vehicleSources.clear();
            virtualRuntimeSources.clear();
            gRead.store(
                gWrite.load(std::memory_order_acquire),
                std::memory_order_release
            );
            ClearCoalescedJobs();
            AcquireSRWLockExclusive(&gCompletionLock);
            gCompletions.clear();
            ReleaseSRWLockExclusive(&gCompletionLock);
            wasPaused = false;
        }
        const auto deviceCheckTime = GetTickCount64();
        const bool recoveryRequested =
            gDeviceRecoveryRequested.exchange(
                false,
                std::memory_order_acq_rel
            );
        if (recoveryRequested ||
            deviceCheckTime >= nextDeviceHealthCheck) {
            nextDeviceHealthCheck = deviceCheckTime + 1000;
            if (recoveryRequested ||
                !IsAudioDeviceHealthy(directSound)) {
                gReady.store(false, std::memory_order_release);
                VirtualizeRuntimeVoices(
                    voices,
                    virtualRuntimeSources,
                    true
                );
                StopAndReleaseVoices(voices, false);
                minigunSources.clear();
                ReleaseBaseBuffers(weaponBaseBuffers);
                ReleaseBaseBuffers(bulletHitBaseBuffers);
                ReleaseVehicleBanks(vehicleBanks);
                if (listener) {
                    listener->Release();
                    listener = nullptr;
                }
                if (directSound) {
                    directSound->Release();
                    directSound = nullptr;
                }

                while (WaitForSingleObject(gStopEvent, 500) !=
                       WAIT_OBJECT_0) {
                    if (CreateAudioDevice(&directSound, &listener)) {
                        gReady.store(true, std::memory_order_release);
                        gDeviceRecoveryRequested.store(
                            false,
                            std::memory_order_release
                        );
                        nextDeviceHealthCheck =
                            GetTickCount64() + 1000;
                        break;
                    }
                }
                if (!directSound || !listener) {
                    break;
                }
            }
        }
        std::string archiveOverride;
        std::string lookupOverride;
        if (TakeBankSourceUpdate(archiveOverride, lookupOverride)) {
            const auto requestedArchivePath = archiveOverride.empty()
                ? gameDirectory + "\\audio\\SFX\\GENRL"
                : archiveOverride;
            const auto requestedLookupPath = lookupOverride.empty()
                ? gameDirectory + "\\audio\\CONFIG\\BankLkup.dat"
                : lookupOverride;
            auto updatedWeapons =
                std::make_unique<OriginalSoundBank>();
            auto updatedBulletHits =
                std::make_unique<OriginalSoundBank>();
            error.clear();
            if (updatedWeapons->Load(
                    requestedLookupPath,
                    requestedArchivePath,
                    kWeaponBankId,
                    error
                ) &&
                updatedBulletHits->Load(
                    requestedLookupPath,
                    requestedArchivePath,
                    kBulletHitBankId,
                    error
                )) {
                ApplyCurrentOverrides(
                    RuntimeSoundBank::Weapons,
                    *updatedWeapons
                );
                ApplyCurrentOverrides(
                    RuntimeSoundBank::BulletHits,
                    *updatedBulletHits
                );
                StopAndReleaseVoices(voices);
                minigunSources.clear();
                ReleaseBaseBuffers(weaponBaseBuffers);
                ReleaseBaseBuffers(bulletHitBaseBuffers);
                weaponBank = std::move(*updatedWeapons);
                bulletHitBank = std::move(*updatedBulletHits);
                activeArchivePath = requestedArchivePath;
                activeLookupPath = requestedLookupPath;
                ReleaseVehicleBanks(vehicleBanks);
            }
        }
        if (TakeDynamicBankUpdate()) {
            ReleaseVehicleBanks(vehicleBanks);
        }
        ApplyPendingOverrides(
            RuntimeSoundBank::Weapons,
            weaponBank,
            weaponBaseBuffers
        );
        ApplyPendingOverrides(
            RuntimeSoundBank::BulletHits,
            bulletHitBank,
            bulletHitBaseBuffers
        );
        const bool replacementIsEnabled =
            gReplaceOriginal.load(std::memory_order_acquire);
        if (replacementIsEnabled != replacementWasEnabled) {
            if (!replacementIsEnabled) {
                StopVoicesByOwner(voices, 0);
                minigunSources.clear();
            }
            replacementWasEnabled = replacementIsEnabled;
        }
        const bool vehicleReplacementIsEnabled =
            gReplaceVehicles.load(std::memory_order_acquire);
        if (vehicleReplacementIsEnabled != vehicleReplacementWasEnabled) {
            if (!vehicleReplacementIsEnabled) {
                StopVoicesByOwner(voices, 1);
                vehicleSources.clear();
            }
            vehicleReplacementWasEnabled = vehicleReplacementIsEnabled;
        }
        const bool dialogueReplacementIsEnabled =
            gReplaceDialogues.load(std::memory_order_acquire);
        if (dialogueReplacementIsEnabled != dialogueReplacementWasEnabled) {
            if (!dialogueReplacementIsEnabled) {
                StopVoicesByOwner(voices, 2);
                virtualRuntimeSources.clear();
            }
            dialogueReplacementWasEnabled = dialogueReplacementIsEnabled;
        }

        WaitForGameAudio();
        auto read = gRead.load(std::memory_order_relaxed);
        const auto write = gWrite.load(std::memory_order_acquire);
        if (!replacementIsEnabled &&
            !vehicleReplacementIsEnabled &&
            !dialogueReplacementIsEnabled) {
            gRead.store(write, std::memory_order_release);
            ClearCoalescedJobs();
            CleanupVoices(voices);
            continue;
        }
        const bool isPaused = IsGamePaused();
        if (isPaused) {
            if (!wasPaused) {
                SuspendVoices(voices);
                wasPaused = true;
            }
            TakeCoalescedJobs(coalescedJobs);
            const auto pausedAt = GetTickCount64();
            for (auto& [key, source] : virtualRuntimeSources) {
                source.lastUpdateAt = pausedAt;
            }
            // Starts before stops: a sound the game started and stopped
            // within one worker pass must end stopped.
            while (read != write) {
                const auto& job = gJobs[read];
                if (dialogueReplacementIsEnabled &&
                    (job.type == AudioJobType::DialogueStart ||
                     job.type == AudioJobType::StatefulStart)) {
                    ProcessDialogueJob(
                        directSound,
                        gameDirectory,
                        activeLookupPath,
                        vehicleBanks,
                        voices,
                        virtualRuntimeSources,
                        job
                    );
                }
                read = (read + 1) & kQueueMask;
            }
            gRead.store(write, std::memory_order_release);
            for (const auto& job : coalescedJobs) {
                if (job.type == AudioJobType::VehicleStop) {
                    ProcessVehicleJob(vehicleSources, voices, job);
                } else if (IsRuntimeStopJob(job.type)) {
                    ProcessDialogueJob(
                        directSound,
                        gameDirectory,
                        activeLookupPath,
                        vehicleBanks,
                        voices,
                        virtualRuntimeSources,
                        job
                    );
                }
            }
            continue;
        }
        if (wasPaused) {
            ResumeVoices(voices);
            // The game's own timers stand still during a pause, so the time
            // paused does not count against a source's heartbeat.
            const auto resumedAt = GetTickCount64();
            for (auto& [key, source] : virtualRuntimeSources) {
                source.lastUpdateAt = resumedAt;
            }
            for (auto& source : minigunSources) {
                source.lastHeartbeat = resumedAt;
            }
            for (auto& [key, source] : vehicleSources) {
                source.lastHeartbeat = resumedAt;
            }
            wasPaused = false;
        }
        TakeCoalescedJobs(coalescedJobs);
        for (const auto& job : coalescedJobs) {
            if (IsRuntimeStopJob(job.type)) {
                continue;
            }
            if (job.type == AudioJobType::VehicleUpdate ||
                job.type == AudioJobType::VehicleStop) {
                if (vehicleReplacementIsEnabled) {
                    ProcessVehicleJob(vehicleSources, voices, job);
                }
            } else if (dialogueReplacementIsEnabled) {
                ProcessDialogueJob(
                    directSound,
                    gameDirectory,
                    activeLookupPath,
                    vehicleBanks,
                    voices,
                    virtualRuntimeSources,
                    job
                );
            }
        }
        while (read != write) {
            const auto& job = gJobs[read];
            if (job.type == AudioJobType::DialogueStart ||
                job.type == AudioJobType::DialogueUpdate ||
                job.type == AudioJobType::DialogueStop ||
                job.type == AudioJobType::StatefulStart ||
                job.type == AudioJobType::StatefulUpdate ||
                job.type == AudioJobType::StatefulStop ||
                job.type == AudioJobType::GenericOneShot) {
                if (dialogueReplacementIsEnabled) {
                    ProcessDialogueJob(
                        directSound,
                        gameDirectory,
                        activeLookupPath,
                        vehicleBanks,
                        voices,
                        virtualRuntimeSources,
                        job
                    );
                }
            } else if (job.type == AudioJobType::VehicleOneShot) {
                if (vehicleReplacementIsEnabled) {
                    ProcessVehicleOneShot(
                        directSound,
                        activeLookupPath,
                        activeArchivePath,
                        vehicleBanks,
                        voices,
                        job
                    );
                }
            } else if (job.type == AudioJobType::VehicleUpdate ||
                       job.type == AudioJobType::VehicleStop) {
                if (vehicleReplacementIsEnabled) {
                    ProcessVehicleJob(vehicleSources, voices, job);
                }
            } else if (!replacementIsEnabled) {
                read = (read + 1) & kQueueMask;
                continue;
            } else if (job.type == AudioJobType::BulletHit) {
                ProcessBulletHit(
                    directSound,
                    bulletHitBank,
                    bulletHitBaseBuffers,
                    voices,
                    job
                );
            } else if (job.minigunMode == MinigunAudioMode::None) {
                ProcessGunLayers(
                    directSound,
                    weaponBank,
                    weaponBaseBuffers,
                    voices,
                    job
                );
            } else {
                ProcessMinigunJob(
                    directSound,
                    weaponBank,
                    weaponBaseBuffers,
                    voices,
                    minigunSources,
                    job
                );
            }
            read = (read + 1) & kQueueMask;
        }
        gRead.store(read, std::memory_order_release);
        if (dialogueReplacementIsEnabled) {
            for (const auto& job : coalescedJobs) {
                if (IsRuntimeStopJob(job.type)) {
                    ProcessDialogueJob(
                        directSound,
                        gameDirectory,
                        activeLookupPath,
                        vehicleBanks,
                        voices,
                        virtualRuntimeSources,
                        job
                    );
                }
            }
        }
        WaitForGameAudio();
        if (vehicleReplacementIsEnabled) {
            ContinueVehicleLoops(
                activeLookupPath,
                activeArchivePath,
                vehicleBanks,
                voices
            );
        }
        CleanupVoices(voices);
        WaitForGameAudio();
        if (replacementIsEnabled) {
            UpdateMinigunSources(
                directSound,
                weaponBank,
                weaponBaseBuffers,
                voices,
                minigunSources
            );
        }
        if (vehicleReplacementIsEnabled) {
            UpdateVehicleSources(
                directSound,
                activeLookupPath,
                activeArchivePath,
                vehicleBanks,
                vehicleSources,
                voices
            );
        }
        if (dialogueReplacementIsEnabled) {
            UpdateVirtualRuntimeSources(
                directSound,
                gameDirectory,
                activeLookupPath,
                vehicleBanks,
                voices,
                virtualRuntimeSources
            );
        }
        WaitForGameAudio();
        UpdateVoicePositions(voices);
        UpdateVoiceEnvironment(voices);
        if (dialogueReplacementIsEnabled) {
            VirtualizeRuntimeVoices(
                voices,
                virtualRuntimeSources,
                false
            );
        }
        WaitForGameAudio();
        RebalanceVoiceMixer(voices);
        UpdateVoiceVolumeFades(voices);
        WaitForGameAudio();
        UpdateVehicleStopFades(voices);
        WaitForGameAudio();
        StartPendingVoices(voices);
    }

    gReady.store(false, std::memory_order_release);
    StopAndReleaseVoices(voices);
    ReleaseBaseBuffers(weaponBaseBuffers);
    ReleaseBaseBuffers(bulletHitBaseBuffers);
    ReleaseVehicleBanks(vehicleBanks);
    if (listener) {
        listener->Release();
    }
    if (directSound) {
        directSound->Release();
    }
    return 0;
}

} // namespace backend
