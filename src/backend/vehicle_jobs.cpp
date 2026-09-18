#include "backend/backend.h"

namespace backend {

void ContinueVehicleLoops(
    const std::string& lookupPath,
    const std::string& archivePath,
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    std::vector<Voice>& voices
) {
    for (auto& voice : voices) {
        if (!voice.vehicleLoopPending ||
            voice.pendingStart ||
            voice.suspended ||
            !voice.buffer) {
            continue;
        }
        DWORD status{};
        if (FAILED(voice.buffer->GetStatus(&status))) {
            RequestDeviceRecovery();
            continue;
        }
        if (!(status & DSBSTATUS_PLAYING)) {
            continue;
        }
        DWORD playCursor{};
        if (FAILED(voice.buffer->GetCurrentPosition(
                &playCursor,
                nullptr
            ))) {
            RequestDeviceRecovery();
            continue;
        }
        if (playCursor >= voice.vehiclePreloopRewriteOffset) {
            continue;
        }
        auto* bank = GetVehicleBank(
            banks,
            lookupPath,
            archivePath,
            voice.vehicleBankId
        );
        const auto* sample = bank
            ? bank->samples.Get(voice.soundId)
            : nullptr;
        const auto preloopBytes = sample && sample->loopStartSample > 0
            ? static_cast<std::size_t>(sample->loopStartSample) * 2
            : 0;
        if (!sample ||
            preloopBytes == 0 ||
            preloopBytes >= sample->pcm.size() ||
            voice.vehiclePreloopRewriteBytes == 0) {
            voice.vehicleLoopPending = false;
            continue;
        }
        const auto loopBytes = sample->pcm.size() - preloopBytes;
        void* first{};
        void* second{};
        DWORD firstSize{};
        DWORD secondSize{};
        if (FAILED(voice.buffer->Lock(
                voice.vehiclePreloopRewriteOffset,
                voice.vehiclePreloopRewriteBytes,
                &first,
                &firstSize,
                &second,
                &secondSize,
                0
            ))) {
            RequestDeviceRecovery();
            continue;
        }
        const auto copyLoopBytes = [&](void* destination,
                                       DWORD size,
                                       std::size_t bufferOffset) {
            auto* output = static_cast<std::uint8_t*>(destination);
            for (DWORD index = 0; index < size; ++index) {
                output[index] = sample->pcm[
                    preloopBytes +
                    (bufferOffset + index) % loopBytes
                ];
            }
        };
        copyLoopBytes(
            first,
            firstSize,
            voice.vehiclePreloopRewriteOffset
        );
        if (second && secondSize) {
            copyLoopBytes(second, secondSize, 0);
        }
        voice.buffer->Unlock(first, firstSize, second, secondSize);
        voice.vehicleLoopPending = false;
    }
}

void ProcessVehicleJob(
    std::unordered_map<std::uintptr_t, VehicleSource>& sources,
    std::vector<Voice>& voices,
    const AudioJob& job
) {
    if (job.type == AudioJobType::VehicleStop) {
        StopVehicleVoice(voices, job.sourceKey);
        sources.erase(job.sourceKey);
        return;
    }
    auto& source = sources[job.sourceKey];
    source.job = job;
    source.lastHeartbeat = GetTickCount64();
}

void ProcessVehicleOneShot(
    IDirectSound8* directSound,
    const std::string& lookupPath,
    const std::string& archivePath,
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    std::vector<Voice>& voices,
    const AudioJob& job
) {
    constexpr std::size_t kMaximumVehicleOneShots = 48;
    if (std::count_if(
            voices.begin(),
            voices.end(),
            [](const Voice& voice) {
                return voice.isVehicleOneShot;
            }
        ) >= kMaximumVehicleOneShots) {
        return;
    }
    auto* bank = GetVehicleBank(
        banks,
        lookupPath,
        archivePath,
        job.bankId
    );
    const auto* sample = bank ? bank->samples.Get(job.drySoundId) : nullptr;
    if (!bank || !sample) {
        return;
    }
    const auto volume =
        job.defaultVolumeDb +
        GetDirectionalMikeAttenuation(job.relativePosition) +
        GetDistanceAttenuation(
            Magnitude(job.relativePosition) / job.baseRollOffFactor
        );
    if (volume <= -100.0f) {
        return;
    }
    const auto listenerVolume =
        volume -
        static_cast<float>(sample->headroom) / 100.0f +
        job.effectsGainDb;
    CreateVehicleVoice(
        directSound,
        *bank,
        voices,
        job,
        listenerVolume
    );
}

} // namespace backend
