#include "backend/backend.h"

namespace backend {

void ProcessGunLayers(
    IDirectSound8* directSound,
    OriginalSoundBank& bank,
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers,
    std::vector<Voice>& voices,
    const AudioJob& job,
    std::uintptr_t minigunSourceKey,
    bool looping) {
    const AudioVector frontLeft{-1.0f, 0.0f, 0.0f};
    const AudioVector frontRight{1.0f, 0.0f, 0.0f};
    const auto outputGainDb = job.effectsGainDb;
    const auto originalBaseVolume =
        job.defaultVolumeDb + job.volumeOffsetDb;

    PlaySample(
        directSound,
        bank,
        baseBuffers,
        voices,
        job.drySoundId,
        job.baseSpeed,
        job.relativePosition,
        CalculateWorldVolume(
            bank,
            job.drySoundId,
            originalBaseVolume,
            job.baseRollOffFactor * (2.0f / 3.0f),
            job.relativePosition
        ) + outputGainDb,
        false,
        outputGainDb,
        job.worldPosition,
        originalBaseVolume,
        job.baseRollOffFactor * (2.0f / 3.0f),
        false,
        minigunSourceKey,
        looping
    );
    PlaySample(
        directSound,
        bank,
        baseBuffers,
        voices,
        job.subSoundId,
        job.baseSpeed,
        job.relativePosition,
        CalculateWorldVolume(
            bank,
            job.subSoundId,
            originalBaseVolume,
            job.baseRollOffFactor * 0.9f,
            job.relativePosition
        ) + outputGainDb,
        false,
        outputGainDb,
        job.worldPosition,
        originalBaseVolume,
        job.baseRollOffFactor * 0.9f,
        false,
        minigunSourceKey,
        looping
    );

    auto worldMainVolume = originalBaseVolume;
    auto frontMainVolume = -100.0f;
    const auto mainRollOff = job.baseRollOffFactor * 1.25f;
    if (!job.isAircraftWeapon) {
        const auto distance = Magnitude(job.relativePosition) / mainRollOff;
        const auto nearEnd = 5.0f / job.baseRollOffFactor;
        const auto blendEnd = 12.0f / job.baseRollOffFactor;
        if (distance < nearEnd) {
            worldMainVolume -= 3.0f;
            frontMainVolume =
                worldMainVolume + GetDistanceAttenuation(distance);
        } else if (distance < blendEnd) {
            const auto blend =
                (blendEnd - distance) / (blendEnd - nearEnd);
            frontMainVolume =
                worldMainVolume +
                GetDistanceAttenuation(distance) +
                std::log10(
                    std::max(blend * 0.70710678118f, 0.00001f)
                ) * 20.0f;
            worldMainVolume +=
                std::log10(
                    (1.0f - blend) * 0.2929f + 0.70710678118f
                ) * 20.0f;
        }
    }

    const auto PlayMain = [&](std::int16_t soundId, bool right) {
        PlaySample(
            directSound,
            bank,
            baseBuffers,
            voices,
            soundId,
            job.mainSpeed,
            right ? frontRight : frontLeft,
            CalculateFrontVolume(bank, soundId, frontMainVolume) +
                outputGainDb,
            true,
            outputGainDb,
            {},
            0.0f,
            0.0f,
            false,
            minigunSourceKey,
            looping
        );
        PlaySample(
            directSound,
            bank,
            baseBuffers,
            voices,
            soundId,
            job.mainSpeed,
            job.relativePosition,
            CalculateWorldVolume(
                bank,
                soundId,
                worldMainVolume,
                mainRollOff,
                job.relativePosition
            ) + outputGainDb,
            false,
            outputGainDb,
            job.worldPosition,
            worldMainVolume,
            mainRollOff,
            false,
            minigunSourceKey,
            looping
        );
    };
    if (job.mainLeftSoundId != -1) {
        PlayMain(job.mainLeftSoundId, false);
    }
    if (job.mainRightSoundId != -1) {
        PlayMain(job.mainRightSoundId, true);
    }

    if (job.tailSoundId != -1 && !job.isAircraftWeapon) {
        const auto tailRollOff = job.baseRollOffFactor * 3.5f;
        // Preserve negative attenuation when clamping x87 tail gain.
        const auto tailSourceVolume = std::min(
            GetDistanceAttenuation(
                Magnitude(job.relativePosition) / tailRollOff
            ) + worldMainVolume - 20.0f,
            0.0f
        );
        const auto tailVolume =
            CalculateFrontVolume(
                bank,
                job.tailSoundId,
                tailSourceVolume
            ) + outputGainDb;
        PlaySample(
            directSound,
            bank,
            baseBuffers,
            voices,
            job.tailSoundId,
            job.tailLeftSpeed,
            frontLeft,
            tailVolume,
            true,
            outputGainDb,
            {},
            0.0f,
            0.0f,
            true,
            minigunSourceKey,
            looping
        );
        PlaySample(
            directSound,
            bank,
            baseBuffers,
            voices,
            job.tailSoundId,
            job.tailRightSpeed,
            frontRight,
            tailVolume,
            true,
            outputGainDb,
            {},
            0.0f,
            0.0f,
            true,
            minigunSourceKey,
            looping
        );
    }
}

void StopMinigunVoices(
    std::vector<Voice>& voices,
    std::uintptr_t sourceKey,
    bool fadeTail
) {
    const auto frame = gEnvironmentFrame.load(std::memory_order_acquire);
    for (auto& voice : voices) {
        if (voice.minigunSourceKey != sourceKey || !voice.buffer) {
            continue;
        }
        if (fadeTail && voice.isTail) {
            voice.minigunTailFading = true;
            voice.minigunFadeFrame = frame;
        } else {
            voice.buffer->Stop();
        }
    }
}

void ProcessMinigunJob(
    IDirectSound8* directSound,
    OriginalSoundBank& bank,
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers,
    std::vector<Voice>& voices,
    std::vector<MinigunSource>& sources,
    const AudioJob& job
) {
    auto source = std::find_if(
        sources.begin(),
        sources.end(),
        [&](const MinigunSource& value) {
            return value.key == job.sourceKey;
        }
    );
    const auto now = GetTickCount64();
    if (source != sources.end() &&
        source->job.minigunMode == job.minigunMode) {
        source->lastHeartbeat = now;
        source->job = job;
        for (auto& voice : voices) {
            if (voice.minigunSourceKey == job.sourceKey) {
                voice.worldPosition = job.worldPosition;
            }
        }
        return;
    }

    if (source != sources.end()) {
        StopMinigunVoices(voices, job.sourceKey, true);
        source->lastHeartbeat = now;
        source->job = job;
    } else {
        sources.push_back({job.sourceKey, now, job});
    }

    if (job.minigunMode == MinigunAudioMode::Fire) {
        ProcessGunLayers(
            directSound,
            bank,
            baseBuffers,
            voices,
            job,
            job.sourceKey,
            true
        );
        return;
    }

    constexpr std::int16_t kMinigunSpinSoundId = 14;
    const auto sourceVolume =
        job.defaultVolumeDb + job.volumeOffsetDb;
    constexpr float kSpinRollOff = 2.0f / 3.0f;
    PlaySample(
        directSound,
        bank,
        baseBuffers,
        voices,
        kMinigunSpinSoundId,
        1.0f,
        job.relativePosition,
        CalculateWorldVolume(
            bank,
            kMinigunSpinSoundId,
            sourceVolume,
            kSpinRollOff,
            job.relativePosition
        ) + job.effectsGainDb,
        false,
        job.effectsGainDb,
        job.worldPosition,
        sourceVolume,
        kSpinRollOff,
        false,
        job.sourceKey,
        true
    );
}

void UpdateMinigunSources(
    IDirectSound8* directSound,
    OriginalSoundBank& bank,
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers,
    std::vector<Voice>& voices,
    std::vector<MinigunSource>& sources
) {
    constexpr DWORD kStopDelayMs = 300;
    constexpr std::int16_t kMinigunStopSoundId = 63;
    const auto now = GetTickCount64();
    for (auto source = sources.begin(); source != sources.end();) {
        if (now - source->lastHeartbeat <= kStopDelayMs) {
            ++source;
            continue;
        }

        StopMinigunVoices(voices, source->key, true);
        const auto& job = source->job;
        const auto speed = job.isAircraftWeapon ? 1.8f : 1.0f;
        const auto rollOff =
            (job.isAircraftWeapon ? 0.7937f : 1.0f) * (2.0f / 3.0f);
        PlaySample(
            directSound,
            bank,
            baseBuffers,
            voices,
            kMinigunStopSoundId,
            speed,
            job.relativePosition,
            CalculateWorldVolume(
                bank,
                kMinigunStopSoundId,
                job.minigunStopVolumeDb,
                rollOff,
                job.relativePosition
            ) + job.effectsGainDb,
            false,
            job.effectsGainDb,
            job.worldPosition,
            job.minigunStopVolumeDb,
            rollOff,
            false
        );
        source = sources.erase(source);
    }

    const auto frame = gEnvironmentFrame.load(std::memory_order_acquire);
    for (auto& voice : voices) {
        if (!voice.minigunTailFading || !voice.buffer) {
            continue;
        }
        const auto elapsedFrames = frame - voice.minigunFadeFrame;
        if (elapsedFrames == 0) {
            continue;
        }
        voice.minigunFadeFrame = frame;
        voice.mixVolumeDb -= 1.5f * static_cast<float>(elapsedFrames);
        if (voice.mixVolumeDb <= -30.0f) {
            voice.buffer->Stop();
        }
    }
}

void ProcessBulletHit(
    IDirectSound8* directSound,
    OriginalSoundBank& bank,
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers,
    std::vector<Voice>& voices,
    const AudioJob& job
) {
    const auto sourceVolume =
        job.defaultVolumeDb + job.volumeOffsetDb;
    const auto listenerVolume = CalculateWorldVolume(
        bank,
        job.drySoundId,
        sourceVolume,
        job.baseRollOffFactor,
        job.relativePosition
    ) + job.effectsGainDb;
    constexpr std::size_t kMaximumBulletHitVoices = 32;
    std::size_t activeBulletHits{};
    auto quietest = voices.end();
    for (auto voice = voices.begin(); voice != voices.end(); ++voice) {
        if (!voice->isBulletHit || !voice->buffer) {
            continue;
        }
        ++activeBulletHits;
        if (quietest == voices.end() ||
            voice->mixVolumeDb < quietest->mixVolumeDb) {
            quietest = voice;
        }
    }
    if (activeBulletHits >= kMaximumBulletHitVoices) {
        if (quietest == voices.end() ||
            listenerVolume <= quietest->mixVolumeDb) {
            return;
        }
        quietest->buffer->Stop();
        quietest->isBulletHit = false;
    }

    PlaySample(
        directSound,
        bank,
        baseBuffers,
        voices,
        job.drySoundId,
        job.baseSpeed,
        job.relativePosition,
        listenerVolume,
        false,
        job.effectsGainDb,
        job.worldPosition,
        sourceVolume,
        job.baseRollOffFactor,
        false,
        0,
        false,
        true
    );
}

} // namespace backend
