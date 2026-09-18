#include "backend/backend.h"

namespace backend {

float Magnitude(const AudioVector& value) {
    return std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z
    );
}

float GetDistanceAttenuation(float distance) {
    constexpr float kResolution = 0.1f;
    constexpr float kMaximumDistance = 128.0f;
    constexpr std::uintptr_t kGameAttenuationTableAddress = 0x8AC270;
    if (!(distance >= 0.0f) || distance >= kMaximumDistance) {
        return -100.0f;
    }
    const auto index = static_cast<std::size_t>(
        std::floor(distance / kResolution)
    );
    return reinterpret_cast<const float*>(
        kGameAttenuationTableAddress
    )[index];
}

float GetDirectionalMikeAttenuation(const AudioVector& direction) {
    constexpr float kCutOff = 0.70710678118f;
    constexpr float kMaximumAttenuation = -6.0f;
    const auto frequency = direction.y;
    if (frequency >= kCutOff) {
        return 0.0f;
    }
    if (frequency <= -kCutOff) {
        return kMaximumAttenuation;
    }
    const auto t = (frequency + kCutOff) / (2.0f * kCutOff);
    return (1.0f - t) * kMaximumAttenuation;
}

float GetHeadroomDb(
    OriginalSoundBank& bank,
    std::int16_t soundId
) {
    const auto* sample = bank.Get(soundId);
    return sample ? static_cast<float>(sample->headroom) / 100.0f : 0.0f;
}

float CalculateWorldVolume(
    OriginalSoundBank& bank,
    std::int16_t soundId,
    float sourceVolumeDb,
    float rollOffFactor,
    const AudioVector& relativePosition
) {
    if (!(rollOffFactor > 0.0f)) {
        return -100.0f;
    }
    return sourceVolumeDb -
           GetHeadroomDb(bank, soundId) +
           GetDirectionalMikeAttenuation(relativePosition) +
           GetDistanceAttenuation(
               Magnitude(relativePosition) / rollOffFactor
           );
}

float CalculateFrontVolume(
    OriginalSoundBank& bank,
    std::int16_t soundId,
    float sourceVolumeDb
) {
    return sourceVolumeDb - GetHeadroomDb(bank, soundId);
}

bool ReadCameraTransform(AudioCameraTransform& transform) {
    const auto generationBefore =
        gCameraTransformGeneration.load(std::memory_order_acquire);
    if (generationBefore == 0 || (generationBefore & 1U)) {
        return false;
    }
    float values[12]{};
    for (std::size_t index = 0; index < std::size(values); ++index) {
        values[index] =
            gCameraTransform[index].load(std::memory_order_relaxed);
    }
    const auto generationAfter =
        gCameraTransformGeneration.load(std::memory_order_acquire);
    if (generationBefore != generationAfter || (generationAfter & 1U)) {
        return false;
    }
    std::memcpy(&transform, values, sizeof(values));
    return true;
}

AudioVector TransformWorldPosition(
    const AudioCameraTransform& transform,
    const AudioVector& position
) {
    return {
        transform.origin.x +
            transform.xAxis.x * position.x +
            transform.yAxis.x * position.y +
            transform.zAxis.x * position.z,
        transform.origin.y +
            transform.xAxis.y * position.x +
            transform.yAxis.y * position.y +
            transform.zAxis.y * position.z,
        transform.origin.z +
            transform.xAxis.z * position.x +
            transform.yAxis.z * position.y +
            transform.zAxis.z * position.z
    };
}

void UpdateVoicePositions(std::vector<Voice>& voices) {
    AudioCameraTransform transform{};
    if (!ReadCameraTransform(transform)) {
        return;
    }
    for (auto& voice : voices) {
        if (!voice.buffer || !voice.spatialBuffer || !voice.followsCamera) {
            continue;
        }
        const auto relative =
            TransformWorldPosition(transform, voice.worldPosition);
        voice.relativePosition = relative;
        AudioCallSucceeded(voice.spatialBuffer->SetPosition(
            relative.x,
            relative.y,
            relative.z,
            DS3D_IMMEDIATE
        ));
        voice.mixVolumeDb =
            voice.sourceVolumeDb -
            voice.headroomDb +
            GetDirectionalMikeAttenuation(relative) +
            GetDistanceAttenuation(
                Magnitude(relative) / voice.rollOffFactor
            );
        if (voice.vehicleSourceKey != 0 && voice.sampleRate != 0) {
            const auto frequency = static_cast<DWORD>(std::clamp(
                static_cast<double>(voice.sampleRate) *
                    std::max(
                        voice.playbackSpeed * voice.dopplerScale,
                        0.05f
                    ),
                static_cast<double>(DSBFREQUENCY_MIN),
                static_cast<double>(DSBFREQUENCY_MAX)
            ));
            AudioCallSucceeded(voice.buffer->SetFrequency(frequency));
        }
    }
}

void UpdateVoiceEnvironment(std::vector<Voice>& voices) {
    const auto frame = gEnvironmentFrame.load(std::memory_order_acquire);
    const bool canSeeOutside =
        gCanSeeOutside.load(std::memory_order_acquire);
    for (auto& voice : voices) {
        if (!voice.isTail || voice.environmentFrame == frame) {
            continue;
        }
        const auto elapsedFrames = frame - voice.environmentFrame;
        voice.environmentFrame = frame;
        if (!canSeeOutside) {
            // Indoor weapon tails decay by 1 dB per audio update.
            voice.mixVolumeDb = std::max(
                voice.mixVolumeDb - static_cast<float>(elapsedFrames),
                -100.0f
            );
        }
    }
}

} // namespace backend
