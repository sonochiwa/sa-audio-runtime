#include "backend/backend.h"

namespace backend {

VehicleBank* GetVehicleBank(
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    const std::string& lookupPath,
    const std::string& archivePath,
    std::int16_t bankId
) {
    if (bankId < 0) {
        return nullptr;
    }
    auto& entry = banks[bankId];
    if (!entry) {
        entry = std::make_unique<VehicleBank>();
    }
    if (!entry->loadAttempted) {
        entry->loadAttempted = true;
        std::string error;
        const auto thread = GetCurrentThread();
        const auto previousPriority = GetThreadPriority(thread);
        if (previousPriority != THREAD_PRIORITY_ERROR_RETURN) {
            SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        }
        entry->loaded = entry->samples.Load(
            lookupPath,
            archivePath,
            static_cast<std::size_t>(bankId),
            error
        );
        if (entry->loaded) {
            ApplyDynamicOverrides(bankId, entry->samples);
        }
        if (previousPriority != THREAD_PRIORITY_ERROR_RETURN) {
            SetThreadPriority(thread, previousPriority);
        }
    }
    return entry->loaded ? entry.get() : nullptr;
}

VehicleBank* GetDialogueBank(
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    const std::string& gameDirectory,
    const std::string& lookupPath,
    std::int16_t bankId
) {
    if (bankId < 0) {
        return nullptr;
    }
    auto& entry = banks[bankId];
    if (!entry) {
        entry = std::make_unique<VehicleBank>();
    }
    if (!entry->loadAttempted) {
        entry->loadAttempted = true;
        std::string error;
        const auto thread = GetCurrentThread();
        const auto previousPriority = GetThreadPriority(thread);
        if (previousPriority != THREAD_PRIORITY_ERROR_RETURN) {
            SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        }
        const auto packId = GetPackIdForBank(bankId);
        const auto* packName = GetPackName(packId);
        std::string archivePath;
        AcquireSRWLockShared(&gOverrideLock);
        if (packId == 1 && !gArchiveOverridePath.empty()) {
            archivePath = gArchiveOverridePath;
        } else if (packId >= 0 &&
            packId < static_cast<int>(gPackOverridePaths.size())) {
            archivePath = gPackOverridePaths[packId];
        }
        ReleaseSRWLockShared(&gOverrideLock);
        if (archivePath.empty() && packName) {
            archivePath = gameDirectory + "\\audio\\SFX\\" + packName;
        }
        entry->loaded = !archivePath.empty() && entry->samples.Load(
            lookupPath,
            archivePath,
            static_cast<std::size_t>(bankId),
            error
        );
        if (entry->loaded) {
            ApplyDynamicOverrides(bankId, entry->samples);
        }
        if (previousPriority != THREAD_PRIORITY_ERROR_RETURN) {
            SetThreadPriority(thread, previousPriority);
        }
    }
    return entry->loaded ? entry.get() : nullptr;
}

Voice* FindVehicleVoice(
    std::vector<Voice>& voices,
    std::uintptr_t sourceKey
) {
    const auto found = std::find_if(
        voices.begin(),
        voices.end(),
        [sourceKey](const Voice& voice) {
            return voice.vehicleSourceKey == sourceKey &&
                   !voice.vehicleStopFading;
        }
    );
    return found != voices.end() ? &*found : nullptr;
}

Voice* FindRestartableVehicleLoop(
    std::vector<Voice>& voices,
    const AudioJob& job
) {
    const auto found = std::find_if(
        voices.begin(),
        voices.end(),
        [&](const Voice& voice) {
            return voice.vehicleSourceKey == job.sourceKey &&
                   voice.looping &&
                   voice.soundId == job.drySoundId &&
                   voice.vehicleBankId == job.bankId;
        }
    );
    return found != voices.end() ? &*found : nullptr;
}

void RestartVehicleLoop(
    Voice& voice,
    const AudioJob& job,
    float listenerVolume
) {
    LONG currentVolume{};
    const auto currentVolumeDb =
        voice.buffer && SUCCEEDED(voice.buffer->GetVolume(&currentVolume))
            ? static_cast<float>(currentVolume) / 100.0f
            : -100.0f;
    voice.vehicleStopFading = false;
    voice.vehicleLoopPending = false;
    voice.vehicleGeneration = job.sourceGeneration;
    voice.worldPosition = job.worldPosition;
    voice.relativePosition = job.relativePosition;
    voice.sourceVolumeDb = job.defaultVolumeDb;
    voice.rollOffFactor = job.baseRollOffFactor;
    voice.outputGainDb = job.effectsGainDb;
    voice.mixVolumeDb = listenerVolume - job.effectsGainDb;
    voice.playbackSpeed = job.baseSpeed;
    voice.dopplerScale = job.dopplerScale;
    voice.volumeFadeActive = true;
    voice.volumeFadeStartedAt = GetTickCount64();
    voice.volumeFadeDurationMs = 12;
    voice.volumeFadeStartDb = currentVolumeDb;
    voice.volumeFadeTargetDb = std::clamp(listenerVolume, -100.0f, 0.0f);
}

std::vector<Voice>::iterator FindQuietestStatefulVoice(
    std::vector<Voice>& voices
) {
    auto quietest = voices.end();
    for (auto voice = voices.begin(); voice != voices.end(); ++voice) {
        if (!voice->isStatefulEffect) {
            continue;
        }
        if (quietest == voices.end() ||
            voice->mixVolumeDb < quietest->mixVolumeDb) {
            quietest = voice;
        }
    }
    return quietest;
}

} // namespace backend
