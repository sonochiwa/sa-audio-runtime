#include "backend/backend.h"

namespace backend {

void VirtualizeRuntimeVoices(
    std::vector<Voice>& voices,
    std::unordered_map<std::uintptr_t, VirtualRuntimeSource>& sources,
    bool force
) {
    const auto now = GetTickCount64();
    for (auto voice = voices.begin(); voice != voices.end();) {
        if (!voice->isRuntimeEffect ||
            !voice->reportsCompletion ||
            !voice->buffer ||
            (!force && voice->mixVolumeDb > -100.0f)) {
            ++voice;
            continue;
        }

        DSBCAPS capabilities{};
        capabilities.dwSize = sizeof(capabilities);
        DWORD playCursor{};
        DWORD durationMs{};
        DWORD cursorMs{};
        if (voice->sampleRate != 0 &&
            SUCCEEDED(voice->buffer->GetCaps(&capabilities)) &&
            SUCCEEDED(voice->buffer->GetCurrentPosition(
                &playCursor,
                nullptr
            ))) {
            durationMs = static_cast<DWORD>(
                static_cast<std::uint64_t>(capabilities.dwBufferBytes) *
                1000 /
                (static_cast<std::uint64_t>(voice->sampleRate) * 2)
            );
            cursorMs = static_cast<DWORD>(
                static_cast<std::uint64_t>(playCursor) * 1000 /
                (static_cast<std::uint64_t>(voice->sampleRate) * 2)
            );
        }

        AudioJob job{};
        job.type = voice->isStatefulEffect
            ? AudioJobType::StatefulStart
            : AudioJobType::DialogueStart;
        job.drySoundId = voice->soundId;
        job.relativePosition = voice->relativePosition;
        job.worldPosition = voice->worldPosition;
        job.defaultVolumeDb = voice->sourceVolumeDb;
        job.baseRollOffFactor = voice->rollOffFactor;
        job.baseSpeed = voice->playbackSpeed;
        job.effectsGainDb = voice->outputGainDb;
        job.sourceKey = voice->vehicleSourceKey;
        job.sourceGeneration = voice->vehicleGeneration;
        job.bankId = voice->vehicleBankId;
        job.dopplerScale = voice->dopplerScale;
        job.isFrontEnd = voice->isFrontEnd;
        job.isUnpausable = voice->isUnpausable;
        sources[job.sourceKey] = {
            job,
            now,
            durationMs,
            static_cast<double>(cursorMs),
            voice->isStatefulEffect &&
                (voice->looping || voice->vehicleLoopPending)
        };

        voice->buffer->Stop();
        if (voice->spatialBuffer) {
            voice->spatialBuffer->Release();
        }
        voice->buffer->Release();
        voice = voices.erase(voice);
    }
}

void UpdateVirtualRuntimeSources(
    IDirectSound8* directSound,
    const std::string& gameDirectory,
    const std::string& lookupPath,
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    std::vector<Voice>& voices,
    std::unordered_map<std::uintptr_t, VirtualRuntimeSource>& sources
) {
    constexpr float kMaterializeThresholdDb = -96.0f;
    const auto now = GetTickCount64();
    for (auto source = sources.begin(); source != sources.end();) {
        auto& state = source->second;
        state.playPositionMs +=
            static_cast<double>(now - state.lastUpdateAt) *
            std::max(static_cast<double>(state.job.baseSpeed), 0.05);
        state.lastUpdateAt = now;
        if (state.looping && state.durationMs != 0) {
            state.playPositionMs = std::fmod(
                state.playPositionMs,
                static_cast<double>(state.durationMs)
            );
        }
        if (!state.looping && state.durationMs != 0 &&
            state.playPositionMs >= state.durationMs) {
            PublishCompletion(state.job);
            source = sources.erase(source);
            continue;
        }

        auto* bank = GetDialogueBank(
            banks,
            gameDirectory,
            lookupPath,
            state.job.bankId
        );
        const auto* sample =
            bank ? bank->samples.Get(state.job.drySoundId) : nullptr;
        if (!bank || !sample) {
            PublishCompletion(state.job);
            source = sources.erase(source);
            continue;
        }
        const auto spatialVolume = state.job.isFrontEnd
            ? state.job.defaultVolumeDb
            : state.job.defaultVolumeDb +
                GetDirectionalMikeAttenuation(
                    state.job.relativePosition
                ) +
                GetDistanceAttenuation(
                    Magnitude(state.job.relativePosition) /
                    state.job.baseRollOffFactor
                );
        const auto listenerVolume =
            spatialVolume -
            static_cast<float>(sample->headroom) / 100.0f +
            state.job.effectsGainDb;
        if (listenerVolume <= kMaterializeThresholdDb) {
            ++source;
            continue;
        }
        if (state.job.type == AudioJobType::StatefulStart) {
            constexpr std::size_t kMaximumStatefulEffects = 64;
            const auto count = std::count_if(
                voices.begin(),
                voices.end(),
                [](const Voice& voice) {
                    return voice.isStatefulEffect;
                }
            );
            if (count >= kMaximumStatefulEffects) {
                const auto quietest =
                    FindQuietestStatefulVoice(voices);
                if (quietest == voices.end() ||
                    !quietest->isStatefulEffect ||
                    quietest->mixVolumeDb >= listenerVolume) {
                    ++source;
                    continue;
                }
                if (quietest->buffer) {
                    quietest->buffer->Stop();
                }
                CleanupVoices(voices);
            }
        }

        AudioJob resumed = state.job;
        if (state.durationMs != 0) {
            resumed.startPercentage = true;
            resumed.playTime = static_cast<std::int16_t>(
                std::min<double>(
                    99,
                    state.playPositionMs * 100.0 / state.durationMs
                )
            );
        }
        if (!CreateVehicleVoice(
                directSound,
                *bank,
                voices,
                resumed,
                listenerVolume
            )) {
            ++source;
            continue;
        }
        source = sources.erase(source);
    }
}

} // namespace backend
