#include "backend/backend.h"

namespace backend {

void UpdateVehicleSources(
    IDirectSound8* directSound,
    const std::string& lookupPath,
    const std::string& archivePath,
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    std::unordered_map<std::uintptr_t, VehicleSource>& sources,
    std::vector<Voice>& voices
) {
    constexpr DWORD kSourceTimeoutMs = 250;
    constexpr std::size_t kMaximumVehicleVoices = 40;
    const auto now = GetTickCount64();
    for (auto source = sources.begin(); source != sources.end();) {
        if (now - source->second.lastHeartbeat <= kSourceTimeoutMs) {
            ++source;
            continue;
        }
        StopVehicleVoice(voices, source->first);
        source = sources.erase(source);
    }

    struct Candidate {
        float volume;
        VehicleSource* source;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(sources.size());
    for (auto& [key, source] : sources) {
        const auto& job = source.job;
        const auto volume =
            job.defaultVolumeDb +
            GetDirectionalMikeAttenuation(job.relativePosition) +
            GetDistanceAttenuation(
                Magnitude(job.relativePosition) / job.baseRollOffFactor
            );
        const auto* voice = FindVehicleVoice(voices, job.sourceKey);
        const auto threshold =
            voice && voice->vehicleGeneration == job.sourceGeneration
                ? -100.0f
                : -96.0f;
        if (volume > threshold) {
            candidates.push_back({volume, &source});
        }
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return left.volume > right.volume;
        }
    );
    if (candidates.size() > kMaximumVehicleVoices) {
        candidates.resize(kMaximumVehicleVoices);
    }

    for (auto& [key, source] : sources) {
        const bool selected = std::any_of(
            candidates.begin(),
            candidates.end(),
            [&](const Candidate& candidate) {
                return candidate.source == &source;
            }
        );
        if (!selected) {
            StopVehicleVoice(voices, key);
        }
    }

    for (const auto& candidate : candidates) {
        auto& source = *candidate.source;
        const auto& job = source.job;
        auto* bank = GetVehicleBank(
            banks,
            lookupPath,
            archivePath,
            job.bankId
        );
        if (!bank) {
            continue;
        }
        const auto* sample = bank->samples.Get(job.drySoundId);
        if (!sample) {
            continue;
        }
        const auto listenerVolume =
            candidate.volume -
            static_cast<float>(sample->headroom) / 100.0f +
            job.effectsGainDb;
        auto* voice = FindVehicleVoice(voices, job.sourceKey);
        if (voice &&
            voice->vehicleGeneration == job.sourceGeneration &&
            voice->soundId == job.drySoundId) {
            voice->worldPosition = job.worldPosition;
            voice->sourceVolumeDb = job.defaultVolumeDb;
            voice->rollOffFactor = job.baseRollOffFactor;
            voice->outputGainDb = job.effectsGainDb;
            voice->playbackSpeed = job.baseSpeed;
            voice->dopplerScale = job.dopplerScale;
            continue;
        }
        if (auto* restartable = FindRestartableVehicleLoop(voices, job)) {
            RestartVehicleLoop(*restartable, job, listenerVolume);
            source.lastStartedGeneration = job.sourceGeneration;
            continue;
        }
        if (voice) {
            StopVehicleVoice(voices, job.sourceKey);
        }
        if (sample->loopStartSample < 0 &&
            source.lastStartedGeneration == job.sourceGeneration) {
            continue;
        }
        if (CreateVehicleVoice(
                directSound,
                *bank,
                voices,
                job,
                listenerVolume
            )) {
            source.lastStartedGeneration = job.sourceGeneration;
            PublishVehiclePlayTimeMetadata(job, *sample);
        }
    }
}

} // namespace backend
