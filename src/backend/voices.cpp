#include "backend/backend.h"

namespace backend {

void CleanupVoices(std::vector<Voice>& voices) {
    voices.erase(
        std::remove_if(
            voices.begin(),
            voices.end(),
            [](Voice& voice) {
                if (voice.pendingStart || voice.suspended) {
                    return false;
                }
                DWORD status{};
                if (voice.buffer &&
                    (FAILED(voice.buffer->GetStatus(&status)) ||
                     (status & DSBSTATUS_BUFFERLOST))) {
                    RequestDeviceRecovery();
                    return false;
                }
                if (!voice.buffer ||
                    !(status & DSBSTATUS_PLAYING)) {
                    if (voice.spatialBuffer) {
                        voice.spatialBuffer->Release();
                    }
                    if (voice.buffer) {
                        voice.buffer->Release();
                    }
                    PublishCompletion(voice);
                    return true;
                }
                return false;
            }
        ),
        voices.end()
    );
}

VoiceGroup GetVoiceGroup(const Voice& voice) {
    if (voice.isRuntimeEffect) {
        return VoiceGroup::Runtime;
    }
    if (voice.vehicleSourceKey != 0) {
        return VoiceGroup::Vehicles;
    }
    return VoiceGroup::Weapons;
}

float GetVoicePriority(const Voice& voice) {
    auto priority = voice.mixVolumeDb + voice.outputGainDb;
    if (voice.isFrontEnd) {
        priority += 24.0f;
    }
    if (voice.looping || voice.vehicleLoopPending) {
        priority += 6.0f;
    }
    if (voice.reportsCompletion) {
        priority += 10.0f;
    }
    if (voice.isBulletHit) {
        priority -= 3.0f;
    }
    if (voice.isTail) {
        priority -= 2.0f;
    }
    return priority;
}

std::size_t GetVoiceGroupLimit(VoiceGroup group) {
    switch (group) {
    case VoiceGroup::Weapons:
        return 48;
    case VoiceGroup::Vehicles:
        return 40;
    case VoiceGroup::Runtime:
        return 48;
    }
    return 0;
}

bool ReserveVoiceSlot(
    std::vector<Voice>& voices,
    VoiceGroup incomingGroup,
    float incomingPriority
) {
    constexpr std::size_t kMaximumPhysicalVoices = 96;
    constexpr std::size_t kGroupReservation = 12;
    std::array<std::size_t, 3> counts{};
    for (const auto& voice : voices) {
        ++counts[static_cast<std::size_t>(GetVoiceGroup(voice))];
    }

    const auto incomingIndex =
        static_cast<std::size_t>(incomingGroup);
    const bool groupIsFull =
        counts[incomingIndex] >= GetVoiceGroupLimit(incomingGroup);
    if (!groupIsFull && voices.size() < kMaximumPhysicalVoices) {
        return true;
    }

    auto candidate = voices.end();
    float candidatePriority{};
    for (auto voice = voices.begin(); voice != voices.end(); ++voice) {
        const auto group = GetVoiceGroup(*voice);
        const auto groupIndex = static_cast<std::size_t>(group);
        if (groupIsFull) {
            if (group != incomingGroup) {
                continue;
            }
        } else if (counts[groupIndex] <= kGroupReservation) {
            continue;
        }

        const auto priority = GetVoicePriority(*voice);
        if (candidate == voices.end() ||
            priority < candidatePriority) {
            candidate = voice;
            candidatePriority = priority;
        }
    }
    if (candidate == voices.end() ||
        candidatePriority >= incomingPriority) {
        return false;
    }

    PublishCompletion(*candidate);
    if (candidate->buffer) {
        candidate->buffer->Stop();
    }
    if (candidate->spatialBuffer) {
        candidate->spatialBuffer->Release();
    }
    if (candidate->buffer) {
        candidate->buffer->Release();
    }
    voices.erase(candidate);
    return true;
}

void PlaySample(
    IDirectSound8* directSound,
    OriginalSoundBank& bank,
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers,
    std::vector<Voice>& voices,
    std::int16_t soundId,
    float speed,
    const AudioVector& position,
    float volumeDb,
    bool forcedFront,
    float outputGainDb,
    const AudioVector& worldPosition,
    float sourceVolumeDb,
    float rollOffFactor,
    bool isTail,
    std::uintptr_t minigunSourceKey,
    bool looping,
    bool isBulletHit) {
    const auto* sample = bank.Get(soundId);
    if (!sample || sample->pcm.empty() || volumeDb <= -100.0f) {
        return;
    }
    auto priority = volumeDb;
    if (forcedFront) {
        priority += 24.0f;
    }
    if (looping) {
        priority += 6.0f;
    }
    if (isBulletHit) {
        priority -= 3.0f;
    }
    if (isTail) {
        priority -= 2.0f;
    }
    if (!ReserveVoiceSlot(
            voices,
            VoiceGroup::Weapons,
            priority
        )) {
        return;
    }

    IDirectSoundBuffer* voice{};
    if (looping && sample->loopStartSample >= 0) {
        OriginalPcmSample loopSample = *sample;
        const auto loopByte = static_cast<std::size_t>(
            sample->loopStartSample
        ) * 2;
        if (loopByte < sample->pcm.size()) {
            loopSample.pcm.assign(
                sample->pcm.begin() + loopByte,
                sample->pcm.end()
            );
        }
        if (!CreateBaseBuffer(directSound, loopSample, &voice)) {
            return;
        }
    } else {
        auto& base = baseBuffers[static_cast<std::size_t>(soundId)];
        if (!EnsureBaseBuffer(directSound, *sample, base)) {
            return;
        }
        if (FAILED(directSound->DuplicateSoundBuffer(base, &voice))) {
            base->Release();
            base = nullptr;
            if (!CreateBaseBuffer(directSound, *sample, &base) ||
                FAILED(directSound->DuplicateSoundBuffer(base, &voice))) {
                return;
            }
        }
    }
    if (!voice) {
        return;
    }

    IDirectSound3DBuffer* spatialBuffer{};
    if (FAILED(voice->QueryInterface(
            IID_IDirectSound3DBuffer,
            reinterpret_cast<void**>(&spatialBuffer)
        )) || !spatialBuffer) {
        voice->Release();
        return;
    }

    const auto frequency = static_cast<DWORD>(std::clamp(
        static_cast<double>(sample->sampleRate) * std::max(speed, 0.05f),
        static_cast<double>(DSBFREQUENCY_MIN),
        static_cast<double>(DSBFREQUENCY_MAX)
    ));
    const auto volume = static_cast<LONG>(
        std::clamp(volumeDb, -100.0f, 0.0f) * 100.0f
    );

    const bool configured =
        AudioCallSucceeded(voice->SetCurrentPosition(0)) &&
        AudioCallSucceeded(voice->SetFrequency(frequency)) &&
        AudioCallSucceeded(voice->SetVolume(std::clamp<LONG>(
            volume,
            DSBVOLUME_MIN,
            DSBVOLUME_MAX
        ))) &&
        AudioCallSucceeded(spatialBuffer->SetMode(
            forcedFront ? DS3DMODE_HEADRELATIVE : DS3DMODE_NORMAL,
            DS3D_IMMEDIATE
        )) &&
        AudioCallSucceeded(spatialBuffer->SetMinDistance(
            1.0f,
            DS3D_IMMEDIATE
        )) &&
        AudioCallSucceeded(spatialBuffer->SetMaxDistance(
            10000.0f,
            DS3D_IMMEDIATE
        )) &&
        AudioCallSucceeded(spatialBuffer->SetPosition(
            position.x,
            position.y,
            position.z,
            DS3D_IMMEDIATE
        ));
    if (!configured) {
        spatialBuffer->Release();
        voice->Release();
        return;
    }
    Voice newVoice{};
    newVoice.buffer = voice;
    newVoice.spatialBuffer = spatialBuffer;
    newVoice.mixVolumeDb = volumeDb - outputGainDb;
    newVoice.outputGainDb = outputGainDb;
    newVoice.isFrontEnd = forcedFront;
    newVoice.pendingStart = true;
    newVoice.soundId = soundId;
    newVoice.headroomDb =
        static_cast<float>(sample->headroom) / 100.0f;
    newVoice.worldPosition = worldPosition;
    newVoice.sourceVolumeDb = sourceVolumeDb;
    newVoice.rollOffFactor = rollOffFactor;
    newVoice.followsCamera =
        !forcedFront && rollOffFactor > 0.0f;
    newVoice.isTail = isTail;
    newVoice.environmentFrame =
        gEnvironmentFrame.load(std::memory_order_acquire);
    newVoice.minigunSourceKey = minigunSourceKey;
    newVoice.looping = looping;
    newVoice.isBulletHit = isBulletHit;
    voices.push_back(newVoice);
}

} // namespace backend
