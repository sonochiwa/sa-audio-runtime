#include "backend/backend.h"

namespace backend {

void StartPendingVoices(std::vector<Voice>& voices) {
    for (auto& voice : voices) {
        if (!voice.pendingStart || !voice.buffer) {
            continue;
        }
        voice.pendingStart = false;
        if (FAILED(voice.buffer->Play(
                0,
                0,
                voice.looping ? DSBPLAY_LOOPING : 0
            ))) {
            voice.pendingStart = true;
            RequestDeviceRecovery();
        }
    }
}

void SuspendVoices(std::vector<Voice>& voices) {
    for (auto& voice : voices) {
        if (!voice.buffer || voice.pendingStart || voice.suspended ||
            voice.isUnpausable) {
            continue;
        }
        DWORD status{};
        const auto statusResult = voice.buffer->GetStatus(&status);
        if (FAILED(statusResult)) {
            RequestDeviceRecovery();
            continue;
        }
        if ((status & DSBSTATUS_PLAYING) &&
            AudioCallSucceeded(voice.buffer->Stop())) {
            voice.suspended = true;
        }
    }
}

void ResumeVoices(std::vector<Voice>& voices) {
    for (auto& voice : voices) {
        if (!voice.buffer || !voice.suspended) {
            continue;
        }
        voice.suspended = false;
        if (FAILED(voice.buffer->Play(
                0,
                0,
                voice.looping ? DSBPLAY_LOOPING : 0
            ))) {
            voice.suspended = true;
            RequestDeviceRecovery();
        }
    }
}

void StopAndReleaseVoices(
    std::vector<Voice>& voices,
    bool publishCompletions) {
    for (auto& voice : voices) {
        if (publishCompletions) {
            PublishCompletion(voice);
        }
        if (voice.buffer) {
            voice.buffer->Stop();
        }
        if (voice.spatialBuffer) {
            voice.spatialBuffer->Release();
            voice.spatialBuffer = nullptr;
        }
        if (voice.buffer) {
            voice.buffer->Release();
            voice.buffer = nullptr;
        }
    }
    voices.clear();
}

void StopVoicesByOwner(
    std::vector<Voice>& voices,
    int ownerGroup
) {
    voices.erase(
        std::remove_if(
            voices.begin(),
            voices.end(),
            [&](Voice& voice) {
                const int voiceGroup = voice.isRuntimeEffect
                    ? 2
                    : (voice.vehicleSourceKey != 0 ? 1 : 0);
                if (voiceGroup != ownerGroup) {
                    return false;
                }
                PublishCompletion(voice);
                if (voice.buffer) {
                    voice.buffer->Stop();
                }
                if (voice.spatialBuffer) {
                    voice.spatialBuffer->Release();
                }
                if (voice.buffer) {
                    voice.buffer->Release();
                }
                return true;
            }
        ),
        voices.end()
    );
}

bool IsGamePaused() {
    constexpr std::uintptr_t kCodePauseAddress = 0xB7CB48;
    constexpr std::uintptr_t kUserPauseAddress = 0xB7CB49;
    return *reinterpret_cast<const volatile std::uint8_t*>(
               kCodePauseAddress
           ) != 0 ||
           *reinterpret_cast<const volatile std::uint8_t*>(
               kUserPauseAddress
           ) != 0;
}

float RebalanceVoiceMixer(std::vector<Voice>& voices) {
    // CAEAudioHardware compressible non-stream voice limit.
    constexpr float kCompressibleMixTarget = 6.4f;
    float amplitudeSum = 0.0f;
    for (const auto& voice : voices) {
        const auto audibleVolume = std::min(voice.mixVolumeDb, 0.0f);
        amplitudeSum += std::pow(
            10.0f,
            audibleVolume / 20.0f
        );
    }

    const auto compressionGainDb =
        amplitudeSum > kCompressibleMixTarget
            ? 20.0f * std::log10(kCompressibleMixTarget / amplitudeSum)
            : 0.0f;
    for (auto& voice : voices) {
        if (!voice.buffer || voice.vehicleStopFading) {
            continue;
        }
        const auto targetVolume = std::clamp(
            std::min(voice.mixVolumeDb, 0.0f) +
                voice.outputGainDb +
                compressionGainDb,
            -100.0f,
            0.0f
        );
        if (voice.volumeFadeActive) {
            if (std::abs(
                    targetVolume - voice.volumeFadeTargetDb
                ) <= 0.01f) {
                continue;
            }
            voice.volumeFadeActive = false;
        }

        LONG currentVolume{};
        DWORD status{};
        const bool isPlaying =
            SUCCEEDED(voice.buffer->GetStatus(&status)) &&
            (status & DSBSTATUS_PLAYING) != 0;
        const auto currentVolumeDb =
            SUCCEEDED(voice.buffer->GetVolume(&currentVolume))
                ? static_cast<float>(currentVolume) / 100.0f
                : targetVolume;
        if (isPlaying &&
            std::abs(targetVolume - currentVolumeDb) > 60.0f) {
            voice.volumeFadeActive = true;
            voice.volumeFadeStartedAt = GetTickCount64();
            voice.volumeFadeDurationMs =
                targetVolume <= currentVolumeDb ? 30u : 28u;
            voice.volumeFadeStartDb = currentVolumeDb;
            voice.volumeFadeTargetDb = targetVolume;
            continue;
        }
        AudioCallSucceeded(voice.buffer->SetVolume(
            static_cast<LONG>(targetVolume * 100.0f)
        ));
    }
    return compressionGainDb;
}

void UpdateVoiceVolumeFades(std::vector<Voice>& voices) {
    const auto now = GetTickCount64();
    for (auto& voice : voices) {
        if (!voice.volumeFadeActive ||
            voice.vehicleStopFading ||
            !voice.buffer) {
            continue;
        }
        const auto elapsed = now - voice.volumeFadeStartedAt;
        if (elapsed >= voice.volumeFadeDurationMs) {
            AudioCallSucceeded(voice.buffer->SetVolume(
                static_cast<LONG>(
                    voice.volumeFadeTargetDb * 100.0f
                )
            ));
            voice.volumeFadeActive = false;
            continue;
        }
        const auto progress =
            static_cast<float>(elapsed) /
            static_cast<float>(voice.volumeFadeDurationMs);
        const auto startAmplitude = std::pow(
            10.0f,
            voice.volumeFadeStartDb / 20.0f
        );
        const auto targetAmplitude = std::pow(
            10.0f,
            voice.volumeFadeTargetDb / 20.0f
        );
        const auto amplitude =
            startAmplitude +
            (targetAmplitude - startAmplitude) * progress;
        const auto volume = std::clamp(
            20.0f * std::log10(std::max(amplitude, 0.00001f)),
            -100.0f,
            0.0f
        );
        AudioCallSucceeded(voice.buffer->SetVolume(
            static_cast<LONG>(volume * 100.0f)
        ));
    }
}

} // namespace backend
