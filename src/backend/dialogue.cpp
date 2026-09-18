#include "backend/backend.h"

namespace backend {

void ProcessDialogueJob(
    IDirectSound8* directSound,
    const std::string& gameDirectory,
    const std::string& lookupPath,
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    std::vector<Voice>& voices,
    std::unordered_map<std::uintptr_t, VirtualRuntimeSource>& virtualSources,
    const AudioJob& job
) {
    auto* voice = FindVehicleVoice(voices, job.sourceKey);
    if (job.type == AudioJobType::DialogueStop ||
        job.type == AudioJobType::StatefulStop) {
        virtualSources.erase(job.sourceKey);
        if (voice && voice->buffer) {
            voice->buffer->Stop();
        }
        return;
    }
    if (job.type == AudioJobType::DialogueUpdate ||
        job.type == AudioJobType::StatefulUpdate) {
        const auto virtualSource = virtualSources.find(job.sourceKey);
        if (virtualSource != virtualSources.end() &&
            virtualSource->second.job.sourceGeneration ==
                job.sourceGeneration) {
            const auto now = GetTickCount64();
            auto& state = virtualSource->second;
            state.playPositionMs +=
                static_cast<double>(now - state.lastUpdateAt) *
                std::max(static_cast<double>(state.job.baseSpeed), 0.05);
            if (state.looping && state.durationMs != 0) {
                state.playPositionMs = std::fmod(
                    state.playPositionMs,
                    static_cast<double>(state.durationMs)
                );
            }
            state.lastUpdateAt = now;
            const auto type = virtualSource->second.job.type;
            virtualSource->second.job = job;
            virtualSource->second.job.type = type;
        }
        if (voice &&
            voice->vehicleGeneration == job.sourceGeneration) {
            voice->worldPosition = job.worldPosition;
            voice->relativePosition = job.relativePosition;
            voice->sourceVolumeDb = job.defaultVolumeDb;
            voice->rollOffFactor = job.baseRollOffFactor;
            voice->outputGainDb = job.effectsGainDb;
            voice->playbackSpeed = job.baseSpeed;
            voice->dopplerScale = job.dopplerScale;
        }
        return;
    }

    if (job.type == AudioJobType::GenericOneShot &&
        std::count_if(
            voices.begin(),
            voices.end(),
            [](const Voice& candidate) {
                return candidate.isRuntimeEffect &&
                       !candidate.reportsCompletion;
            }
        ) >= 64) {
        return;
    }

    virtualSources.erase(job.sourceKey);
    if (voice && voice->buffer) {
        voice->buffer->Stop();
        CleanupVoices(voices);
    }
    auto* bank = GetDialogueBank(
        banks,
        gameDirectory,
        lookupPath,
        job.bankId
    );
    const auto* sample = bank ? bank->samples.Get(job.drySoundId) : nullptr;
    if (!bank || !sample) {
        if (job.type == AudioJobType::DialogueStart ||
            job.type == AudioJobType::StatefulStart) {
            PublishCompletion(job);
        }
        return;
    }
    const auto spatialVolume = job.isFrontEnd
        ? job.defaultVolumeDb
        : job.defaultVolumeDb +
            GetDirectionalMikeAttenuation(job.relativePosition) +
            GetDistanceAttenuation(
                Magnitude(job.relativePosition) / job.baseRollOffFactor
            );
    const auto listenerVolume =
        spatialVolume -
        static_cast<float>(sample->headroom) / 100.0f +
        job.effectsGainDb;
    if ((job.type == AudioJobType::DialogueStart ||
         job.type == AudioJobType::StatefulStart) &&
        listenerVolume <= -100.0f) {
        const auto samples = sample->pcm.size() / sizeof(std::int16_t);
        const auto duration = sample->sampleRate != 0
            ? static_cast<double>(samples) * 1000.0 /
                static_cast<double>(sample->sampleRate)
            : 0.0;
        const auto durationMs = static_cast<DWORD>(std::clamp(
            duration,
            0.0,
            static_cast<double>(MAXDWORD)
        ));
        virtualSources[job.sourceKey] = {
            job,
            GetTickCount64(),
            durationMs,
            job.startPercentage && durationMs != 0
                ? static_cast<double>(durationMs) *
                    static_cast<std::uint16_t>(job.playTime) / 100.0
                : 0.0,
            job.type == AudioJobType::StatefulStart &&
                sample->loopStartSample >= 0
        };
        PublishDialogueStarted(job, *sample);
        return;
    }
    if (job.type == AudioJobType::StatefulStart) {
        constexpr std::size_t kMaximumStatefulEffects = 64;
        const auto count = std::count_if(
            voices.begin(),
            voices.end(),
            [](const Voice& candidate) {
                return candidate.isStatefulEffect;
            }
        );
        if (count >= kMaximumStatefulEffects) {
            const auto quietest = FindQuietestStatefulVoice(voices);
            if (quietest == voices.end() ||
                !quietest->isStatefulEffect ||
                quietest->mixVolumeDb >= listenerVolume) {
                PublishCompletion(job);
                return;
            }
            if (quietest->buffer) {
                quietest->buffer->Stop();
            }
            CleanupVoices(voices);
        }
    }
    if (!CreateVehicleVoice(
            directSound,
            *bank,
            voices,
            job,
            listenerVolume
        )) {
        if (job.type == AudioJobType::DialogueStart ||
            job.type == AudioJobType::StatefulStart) {
            PublishCompletion(job);
        }
    } else if (job.type == AudioJobType::DialogueStart ||
               job.type == AudioJobType::StatefulStart) {
        PublishDialogueStarted(job, *sample);
    }
}

} // namespace backend
