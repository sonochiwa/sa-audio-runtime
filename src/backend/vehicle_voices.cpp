#include "backend/backend.h"

namespace backend {

void StopVehicleVoice(
    std::vector<Voice>& voices,
    std::uintptr_t sourceKey
) {
    const auto now = GetTickCount64();
    for (auto voice = voices.begin(); voice != voices.end();) {
        if (voice->vehicleSourceKey != sourceKey ||
            voice->vehicleStopFading) {
            ++voice;
            continue;
        }
        voice->vehicleLoopPending = false;
        DWORD status{};
        const bool canFade =
            voice->buffer &&
            !voice->pendingStart &&
            !voice->suspended &&
            SUCCEEDED(voice->buffer->GetStatus(&status)) &&
            (status & DSBSTATUS_PLAYING);
        if (canFade) {
            voice->volumeFadeActive = false;
            LONG currentVolume{};
            if (SUCCEEDED(voice->buffer->GetVolume(&currentVolume))) {
                voice->vehicleStopFadeStartDb =
                    static_cast<float>(currentVolume) / 100.0f;
            }
            voice->vehicleStopFading = true;
            voice->vehicleStopFadeStartedAt = now;
            ++voice;
            continue;
        }
        if (voice->buffer) {
            voice->buffer->Stop();
        }
        if (voice->spatialBuffer) {
            voice->spatialBuffer->Release();
        }
        if (voice->buffer) {
            voice->buffer->Release();
        }
        voice = voices.erase(voice);
    }
}

void UpdateVehicleStopFades(std::vector<Voice>& voices) {
    constexpr ULONGLONG kSoftwareFadeTimeMs = 30;
    const auto now = GetTickCount64();
    for (auto voice = voices.begin(); voice != voices.end();) {
        if (!voice->vehicleStopFading) {
            ++voice;
            continue;
        }
        const auto elapsed = now - voice->vehicleStopFadeStartedAt;
        if (elapsed < kSoftwareFadeTimeMs && voice->buffer) {
            const auto progress =
                static_cast<float>(elapsed) /
                static_cast<float>(kSoftwareFadeTimeMs);
            const auto startAmplitude = std::pow(
                10.0f,
                voice->vehicleStopFadeStartDb / 20.0f
            );
            const auto amplitude = std::max(
                startAmplitude * (1.0f - progress),
                0.00001f
            );
            const auto volume = static_cast<LONG>(
                std::clamp(
                    20.0f * std::log10(amplitude),
                    -100.0f,
                    0.0f
                ) * 100.0f
            );
            AudioCallSucceeded(voice->buffer->SetVolume(volume));
            ++voice;
            continue;
        }
        if (voice->buffer) {
            voice->buffer->Stop();
        }
        if (voice->spatialBuffer) {
            voice->spatialBuffer->Release();
        }
        if (voice->buffer) {
            voice->buffer->Release();
        }
        voice = voices.erase(voice);
    }
}

bool CreateVehicleVoice(
    IDirectSound8* directSound,
    VehicleBank& bank,
    std::vector<Voice>& voices,
    const AudioJob& job,
    float listenerVolume
) {
    const auto* sample = bank.samples.Get(job.drySoundId);
    if (!sample || sample->pcm.empty()) {
        return false;
    }

    const bool hasLoop =
        sample->loopStartSample >= 0 &&
        (job.type == AudioJobType::VehicleUpdate ||
         job.type == AudioJobType::StatefulStart);
    const bool delayedLoop = hasLoop && sample->loopStartSample > 0;
    const bool runtimeVoice =
        job.type == AudioJobType::DialogueStart ||
        job.type == AudioJobType::StatefulStart ||
        job.type == AudioJobType::GenericOneShot;
    auto priority = listenerVolume;
    if (job.isFrontEnd) {
        priority += 24.0f;
    }
    if (hasLoop) {
        priority += 6.0f;
    }
    if (job.type == AudioJobType::DialogueStart ||
        job.type == AudioJobType::StatefulStart) {
        priority += 10.0f;
    }
    if (!ReserveVoiceSlot(
            voices,
            runtimeVoice
                ? VoiceGroup::Runtime
                : VoiceGroup::Vehicles,
            priority
        )) {
        return false;
    }
    IDirectSoundBuffer* buffer{};
    DWORD initialPosition{};
    DWORD preloopRewriteOffset{};
    DWORD preloopRewriteBytes{};
    if (delayedLoop) {
        if (!CreatePreloopBuffer(
                directSound,
                *sample,
                &buffer,
                initialPosition,
                preloopRewriteOffset,
                preloopRewriteBytes
            )) {
            return false;
        }
    } else {
        auto& base =
            bank.baseBuffers[static_cast<std::size_t>(job.drySoundId)];
        if (!EnsureBaseBuffer(directSound, *sample, base) ||
            FAILED(directSound->DuplicateSoundBuffer(base, &buffer))) {
            if (base) {
                base->Release();
                base = nullptr;
            }
            return false;
        }
    }

    IDirectSound3DBuffer* spatialBuffer{};
    if (FAILED(buffer->QueryInterface(
            IID_IDirectSound3DBuffer,
            reinterpret_cast<void**>(&spatialBuffer)
        )) || !spatialBuffer) {
        buffer->Release();
        return false;
    }

    const auto frequency = static_cast<DWORD>(std::clamp(
        static_cast<double>(sample->sampleRate) *
            std::max(job.baseSpeed, 0.05f),
        static_cast<double>(DSBFREQUENCY_MIN),
        static_cast<double>(DSBFREQUENCY_MAX)
    ));
    const auto initialVolume = hasLoop ? -100.0f : listenerVolume;
    if (!AudioCallSucceeded(buffer->SetFrequency(frequency)) ||
        !AudioCallSucceeded(buffer->SetVolume(static_cast<LONG>(
            std::clamp(initialVolume, -100.0f, 0.0f) * 100.0f
        )))) {
        spatialBuffer->Release();
        buffer->Release();
        return false;
    }
    if (delayedLoop) {
        if (!AudioCallSucceeded(
                buffer->SetCurrentPosition(initialPosition)
            )) {
            spatialBuffer->Release();
            buffer->Release();
            return false;
        }
    } else if (job.playTime > 0) {
        DSBCAPS capabilities{};
        capabilities.dwSize = sizeof(capabilities);
        if (SUCCEEDED(buffer->GetCaps(&capabilities))) {
            auto position = job.startPercentage
                ? static_cast<DWORD>(
                      static_cast<std::uint64_t>(
                          capabilities.dwBufferBytes
                      ) *
                      static_cast<std::uint16_t>(job.playTime) /
                      100u
                  )
                : static_cast<DWORD>(std::min<std::uint64_t>(
                      static_cast<std::uint64_t>(job.playTime) *
                          sample->sampleRate * 2u /
                          1000u,
                      capabilities.dwBufferBytes > 1
                          ? capabilities.dwBufferBytes - 2u
                          : 0u
                  ));
            position &= ~1u;
            if (position < capabilities.dwBufferBytes &&
                !AudioCallSucceeded(
                    buffer->SetCurrentPosition(position)
                )) {
                spatialBuffer->Release();
                buffer->Release();
                return false;
            }
        }
    } else if (!AudioCallSucceeded(buffer->SetCurrentPosition(0))) {
        spatialBuffer->Release();
        buffer->Release();
        return false;
    }
    if (!AudioCallSucceeded(spatialBuffer->SetMode(
            job.isFrontEnd ? DS3DMODE_HEADRELATIVE : DS3DMODE_NORMAL,
            DS3D_IMMEDIATE
        )) ||
        !AudioCallSucceeded(spatialBuffer->SetMinDistance(
            1.0f,
            DS3D_IMMEDIATE
        )) ||
        !AudioCallSucceeded(spatialBuffer->SetMaxDistance(
            10000.0f,
            DS3D_IMMEDIATE
        )) ||
        !AudioCallSucceeded(spatialBuffer->SetPosition(
            job.relativePosition.x,
            job.isFrontEnd && job.relativePosition.y == 0.0f
                ? 1.0f
                : job.relativePosition.y,
            job.relativePosition.z,
            DS3D_IMMEDIATE
        ))) {
        spatialBuffer->Release();
        buffer->Release();
        return false;
    }

    Voice voice{};
    voice.buffer = buffer;
    voice.spatialBuffer = spatialBuffer;
    voice.mixVolumeDb = listenerVolume - job.effectsGainDb;
    voice.outputGainDb = job.effectsGainDb;
    voice.pendingStart = true;
    voice.soundId = job.drySoundId;
    voice.headroomDb = static_cast<float>(sample->headroom) / 100.0f;
    voice.worldPosition = job.worldPosition;
    voice.relativePosition = job.relativePosition;
    voice.sourceVolumeDb = job.defaultVolumeDb;
    voice.rollOffFactor = job.baseRollOffFactor;
    voice.followsCamera = !job.isFrontEnd;
    voice.isFrontEnd = job.isFrontEnd;
    voice.isUnpausable = job.isUnpausable;
    voice.looping = hasLoop;
    voice.vehicleSourceKey = job.sourceKey;
    voice.isVehicleOneShot =
        job.type == AudioJobType::VehicleOneShot ||
        job.type == AudioJobType::GenericOneShot;
    voice.vehicleGeneration = job.sourceGeneration;
    voice.sampleRate = sample->sampleRate;
    voice.playbackSpeed = job.baseSpeed;
    voice.dopplerScale = job.dopplerScale;
    voice.vehicleBankId = job.bankId;
    voice.vehicleLoopPending = delayedLoop;
    voice.vehiclePreloopRewriteOffset = preloopRewriteOffset;
    voice.vehiclePreloopRewriteBytes = preloopRewriteBytes;
    if (hasLoop) {
        voice.volumeFadeActive = true;
        voice.volumeFadeStartedAt = GetTickCount64();
        voice.volumeFadeDurationMs = 12;
        voice.volumeFadeStartDb = -100.0f;
        voice.volumeFadeTargetDb = std::clamp(
            listenerVolume,
            -100.0f,
            0.0f
        );
    }
    voice.isRuntimeEffect = runtimeVoice;
    voice.isStatefulEffect =
        job.type == AudioJobType::StatefulStart;
    voice.reportsCompletion =
        job.type == AudioJobType::DialogueStart ||
        job.type == AudioJobType::StatefulStart;
    voices.push_back(voice);
    return true;
}

} // namespace backend
