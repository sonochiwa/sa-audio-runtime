#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "weapon_backend.h"
#include "sound_bank.h"

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kQueueCapacity = 1024;
constexpr std::uint32_t kQueueMask = kQueueCapacity - 1;
constexpr std::size_t kMaxOriginalSounds = 400;
constexpr std::size_t kWeaponBankId = 143;
constexpr std::size_t kBulletHitBankId = 27;

struct Voice {
    IDirectSoundBuffer* buffer{};
    IDirectSound3DBuffer* spatialBuffer{};
    float mixVolumeDb{-100.0f};
    float outputGainDb{};
    bool isFrontEnd{};
    float normalizationReferenceDb{};
    bool pendingStart{};
    bool suspended{};
    std::int16_t soundId{-1};
    float headroomDb{};
    AudioVector worldPosition{};
    float sourceVolumeDb{};
    float rollOffFactor{};
    bool followsCamera{};
    bool isTail{};
    std::uint32_t environmentFrame{};
    std::uintptr_t minigunSourceKey{};
    bool looping{};
    bool minigunTailFading{};
    std::uint32_t minigunFadeFrame{};
    bool isBulletHit{};
};

struct MinigunSource {
    std::uintptr_t key{};
    DWORD lastHeartbeat{};
    AudioJob job{};
};

HMODULE gModule{};
HANDLE gThread{};
HANDLE gWakeEvent{};
HANDLE gStopEvent{};
std::array<AudioJob, kQueueCapacity> gJobs{};
std::atomic<std::uint32_t> gWrite{};
std::atomic<std::uint32_t> gRead{};
std::atomic<bool> gReady{};
std::atomic<bool> gReplaceOriginal{};
std::array<std::atomic<float>, 12> gCameraTransform{};
std::atomic<std::uint32_t> gCameraTransformGeneration{};
std::atomic<bool> gCanSeeOutside{true};
std::atomic<std::uint32_t> gEnvironmentFrame{};
SRWLOCK gOverrideLock = SRWLOCK_INIT;
constexpr auto kRuntimeSoundBankCount =
    static_cast<std::size_t>(RuntimeSoundBank::Count);
std::array<
    std::array<std::string, kMaxOriginalSounds>,
    kRuntimeSoundBankCount
> gOverridePaths{};
std::array<
    std::array<std::int8_t, kMaxOriginalSounds>,
    kRuntimeSoundBankCount
> gOverrideActions{};
std::string gArchiveOverridePath;
std::string gLookupOverridePath;
bool gBankSourcesDirty{};

std::string GetModuleDirectory() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(gModule, path, MAX_PATH);
    if (auto* slash = std::strrchr(path, '\\')) {
        *slash = '\0';
    }
    return path;
}

std::string GetGameDirectory() {
    auto directory = GetModuleDirectory();
    if (const auto slash = directory.find_last_of('\\'); slash != std::string::npos) {
        directory.resize(slash);
    }
    return directory;
}

HWND FindProcessWindow() {
    struct Search {
        DWORD processId;
        HWND window;
    } search{GetCurrentProcessId(), nullptr};

    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            auto& state = *reinterpret_cast<Search*>(parameter);
            DWORD processId{};
            GetWindowThreadProcessId(window, &processId);
            if (processId == state.processId && IsWindowVisible(window) &&
                GetWindow(window, GW_OWNER) == nullptr) {
                state.window = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search)
    );
    return search.window;
}

bool CreateBaseBuffer(
    IDirectSound8* directSound,
    const OriginalPcmSample& sample,
    IDirectSoundBuffer** output
) {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = sample.sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    DSBUFFERDESC description{};
    description.dwSize = sizeof(description);
    description.dwFlags =
        DSBCAPS_CTRLVOLUME |
        DSBCAPS_CTRL3D |
        DSBCAPS_CTRLFREQUENCY |
        DSBCAPS_GLOBALFOCUS |
        DSBCAPS_GETCURRENTPOSITION2 |
        DSBCAPS_LOCSOFTWARE;
    description.dwBufferBytes = static_cast<DWORD>(sample.pcm.size());
    description.lpwfxFormat = &format;

    IDirectSoundBuffer* buffer{};
    if (FAILED(directSound->CreateSoundBuffer(&description, &buffer, nullptr))) {
        return false;
    }

    void* first{};
    void* second{};
    DWORD firstSize{};
    DWORD secondSize{};
    if (FAILED(buffer->Lock(
            0,
            description.dwBufferBytes,
            &first,
            &firstSize,
            &second,
            &secondSize,
            0
        ))) {
        buffer->Release();
        return false;
    }

    std::memcpy(first, sample.pcm.data(), firstSize);
    if (second && secondSize) {
        std::memcpy(
            second,
            sample.pcm.data() + firstSize,
            secondSize
        );
    }
    buffer->Unlock(first, firstSize, second, secondSize);
    *output = buffer;
    return true;
}

bool EnsureBaseBuffer(
    IDirectSound8* directSound,
    const OriginalPcmSample& sample,
    IDirectSoundBuffer*& buffer
) {
    if (buffer) {
        DWORD status{};
        if (SUCCEEDED(buffer->GetStatus(&status)) &&
            !(status & DSBSTATUS_BUFFERLOST)) {
            return true;
        }
        buffer->Release();
        buffer = nullptr;
    }
    return CreateBaseBuffer(directSound, sample, &buffer);
}

void ApplyPendingOverrides(
    RuntimeSoundBank runtimeBank,
    OriginalSoundBank& bank,
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers
) {
    const auto bankIndex = static_cast<std::size_t>(runtimeBank);
    std::array<std::string, kMaxOriginalSounds> paths{};
    std::array<std::int8_t, kMaxOriginalSounds> actions{};
    AcquireSRWLockExclusive(&gOverrideLock);
    for (std::size_t index = 0; index < kMaxOriginalSounds; ++index) {
        if (gOverrideActions[bankIndex][index] != 0) {
            actions[index] = gOverrideActions[bankIndex][index];
            paths[index] = std::move(gOverridePaths[bankIndex][index]);
            gOverrideActions[bankIndex][index] = 0;
        }
    }
    ReleaseSRWLockExclusive(&gOverrideLock);

    for (std::size_t index = 0; index < kMaxOriginalSounds; ++index) {
        if (actions[index] == 0) {
            continue;
        }
        bool changed{};
        std::string error;
        if (actions[index] > 0) {
            changed = bank.ApplyWaveOverride(
                static_cast<std::int16_t>(index),
                paths[index],
                error
            );
        } else {
            changed = bank.RestoreOriginal(
                static_cast<std::int16_t>(index)
            );
        }
        if (changed && baseBuffers[index]) {
            baseBuffers[index]->Release();
            baseBuffers[index] = nullptr;
        }
    }
}

void ApplyCurrentOverrides(
    RuntimeSoundBank runtimeBank,
    OriginalSoundBank& bank
) {
    const auto bankIndex = static_cast<std::size_t>(runtimeBank);
    std::array<std::string, kMaxOriginalSounds> paths{};
    AcquireSRWLockShared(&gOverrideLock);
    paths = gOverridePaths[bankIndex];
    ReleaseSRWLockShared(&gOverrideLock);

    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (paths[index].empty()) {
            continue;
        }
        std::string error;
        bank.ApplyWaveOverride(
            static_cast<std::int16_t>(index),
            paths[index],
            error
        );
    }
}

bool TakeBankSourceUpdate(
    std::string& archivePath,
    std::string& lookupPath
) {
    AcquireSRWLockExclusive(&gOverrideLock);
    const bool changed = gBankSourcesDirty;
    if (changed) {
        archivePath = gArchiveOverridePath;
        lookupPath = gLookupOverridePath;
        gBankSourcesDirty = false;
    }
    ReleaseSRWLockExclusive(&gOverrideLock);
    return changed;
}

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
                if (!voice.buffer ||
                    FAILED(voice.buffer->GetStatus(&status)) ||
                    (status & DSBSTATUS_BUFFERLOST) ||
                    !(status & DSBSTATUS_PLAYING)) {
                    if (voice.spatialBuffer) {
                        voice.spatialBuffer->Release();
                    }
                    if (voice.buffer) {
                        voice.buffer->Release();
                    }
                    return true;
                }
                return false;
            }
        ),
        voices.end()
    );
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
    std::uintptr_t minigunSourceKey = 0,
    bool looping = false,
    bool isBulletHit = false
) {
    const auto* sample = bank.Get(soundId);
    if (!sample || sample->pcm.empty() || volumeDb <= -100.0f) {
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

    voice->SetCurrentPosition(0);
    voice->SetFrequency(frequency);
    voice->SetVolume(std::clamp<LONG>(volume, DSBVOLUME_MIN, DSBVOLUME_MAX));
    spatialBuffer->SetMode(
        forcedFront ? DS3DMODE_HEADRELATIVE : DS3DMODE_NORMAL,
        DS3D_IMMEDIATE
    );
    spatialBuffer->SetMinDistance(1.0f, DS3D_IMMEDIATE);
    spatialBuffer->SetMaxDistance(10000.0f, DS3D_IMMEDIATE);
    spatialBuffer->SetPosition(
        position.x,
        position.y,
        position.z,
        DS3D_IMMEDIATE
    );
    // Start layers together after normalization, matching SynchPlayback.
    voices.push_back({
        voice,
        spatialBuffer,
        volumeDb - outputGainDb,
        outputGainDb,
        forcedFront,
        0.0f,
        true,
        false,
        soundId,
        static_cast<float>(sample->headroom) / 100.0f,
        worldPosition,
        sourceVolumeDb,
        rollOffFactor,
        !forcedFront && rollOffFactor > 0.0f,
        isTail,
        gEnvironmentFrame.load(std::memory_order_acquire),
        minigunSourceKey,
        looping,
        false,
        0,
        isBulletHit
    });
}

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
            if (voice.spatialBuffer) {
                voice.spatialBuffer->Release();
                voice.spatialBuffer = nullptr;
            }
            voice.buffer->Release();
            voice.buffer = nullptr;
        }
    }
}

void SuspendVoices(std::vector<Voice>& voices) {
    for (auto& voice : voices) {
        if (!voice.buffer || voice.pendingStart || voice.suspended) {
            continue;
        }
        DWORD status{};
        if (SUCCEEDED(voice.buffer->GetStatus(&status)) &&
            (status & DSBSTATUS_PLAYING) &&
            SUCCEEDED(voice.buffer->Stop())) {
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
            if (voice.spatialBuffer) {
                voice.spatialBuffer->Release();
                voice.spatialBuffer = nullptr;
            }
            voice.buffer->Release();
            voice.buffer = nullptr;
        }
    }
}

void StopAndReleaseVoices(std::vector<Voice>& voices) {
    for (auto& voice : voices) {
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
        const auto finalVolume = static_cast<LONG>(
            std::clamp(
                std::min(voice.mixVolumeDb, 0.0f) +
                    voice.outputGainDb +
                    compressionGainDb,
                -100.0f,
                0.0f
            ) * 100.0f
        );
        voice.buffer->SetVolume(finalVolume);
    }
    return compressionGainDb;
}

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
        voice.spatialBuffer->SetPosition(
            relative.x,
            relative.y,
            relative.z,
            DS3D_IMMEDIATE
        );
        voice.mixVolumeDb =
            voice.sourceVolumeDb -
            voice.headroomDb +
            GetDirectionalMikeAttenuation(relative) +
            GetDistanceAttenuation(
                Magnitude(relative) / voice.rollOffFactor
            );
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

void ProcessGunLayers(
    IDirectSound8* directSound,
    OriginalSoundBank& bank,
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers,
    std::vector<Voice>& voices,
    const AudioJob& job,
    std::uintptr_t minigunSourceKey = 0,
    bool looping = false
) {
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
    const auto now = GetTickCount();
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
    const auto now = GetTickCount();
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

bool InitialiseListener(
    IDirectSound8* directSound,
    IDirectSound3DListener** output
) {
    DSBUFFERDESC description{};
    description.dwSize = sizeof(description);
    description.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_PRIMARYBUFFER;

    IDirectSoundBuffer* primary{};
    if (FAILED(directSound->CreateSoundBuffer(
            &description,
            &primary,
            nullptr
        ))) {
        return false;
    }

    IDirectSound3DListener* listener{};
    const auto result = primary->QueryInterface(
        IID_IDirectSound3DListener,
        reinterpret_cast<void**>(&listener)
    );
    primary->Release();
    if (FAILED(result) || !listener) {
        return false;
    }

    listener->SetPosition(0.0f, 0.0f, 0.0f, DS3D_IMMEDIATE);
    listener->SetOrientation(
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        -1.0f,
        DS3D_IMMEDIATE
    );
    listener->SetRolloffFactor(0.0f, DS3D_IMMEDIATE);
    listener->SetDopplerFactor(0.0f, DS3D_IMMEDIATE);
    *output = listener;
    return true;
}

DWORD WINAPI BackendThread(void*) {
    OriginalSoundBank weaponBank;
    OriginalSoundBank bulletHitBank;
    std::string error;
    const auto gameDirectory = GetGameDirectory();
    if (!weaponBank.Load(gameDirectory, kWeaponBankId, error) ||
        !bulletHitBank.Load(gameDirectory, kBulletHitBankId, error)) {
        return 1;
    }

    IDirectSound8* directSound{};
    if (FAILED(DirectSoundCreate8(nullptr, &directSound, nullptr))) {
        return 2;
    }

    HWND window{};
    for (int attempt = 0; attempt < 100 && !window; ++attempt) {
        window = FindProcessWindow();
        if (!window) {
            Sleep(50);
        }
    }
    if (!window ||
        FAILED(directSound->SetCooperativeLevel(window, DSSCL_NORMAL))) {
        directSound->Release();
        return 3;
    }

    IDirectSound3DListener* listener{};
    if (!InitialiseListener(directSound, &listener)) {
        directSound->Release();
        return 4;
    }

    std::array<IDirectSoundBuffer*, kMaxOriginalSounds> weaponBaseBuffers{};
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds> bulletHitBaseBuffers{};
    std::vector<Voice> voices;
    std::vector<MinigunSource> minigunSources;
    voices.reserve(128);
    minigunSources.reserve(16);
    gReady.store(true, std::memory_order_release);

    HANDLE waits[] = {gStopEvent, gWakeEvent};
    bool wasPaused{};
    bool replacementWasEnabled =
        gReplaceOriginal.load(std::memory_order_acquire);
    while (WaitForMultipleObjects(2, waits, FALSE, 10) != WAIT_OBJECT_0) {
        std::string archiveOverride;
        std::string lookupOverride;
        if (TakeBankSourceUpdate(archiveOverride, lookupOverride)) {
            const auto archivePath = archiveOverride.empty()
                ? gameDirectory + "\\audio\\SFX\\GENRL"
                : archiveOverride;
            const auto lookupPath = lookupOverride.empty()
                ? gameDirectory + "\\audio\\CONFIG\\BankLkup.dat"
                : lookupOverride;
            OriginalSoundBank updatedWeapons;
            OriginalSoundBank updatedBulletHits;
            error.clear();
            if (updatedWeapons.Load(
                    lookupPath,
                    archivePath,
                    kWeaponBankId,
                    error
                ) &&
                updatedBulletHits.Load(
                    lookupPath,
                    archivePath,
                    kBulletHitBankId,
                    error
                )) {
                ApplyCurrentOverrides(
                    RuntimeSoundBank::Weapons,
                    updatedWeapons
                );
                ApplyCurrentOverrides(
                    RuntimeSoundBank::BulletHits,
                    updatedBulletHits
                );
                StopAndReleaseVoices(voices);
                minigunSources.clear();
                for (auto*& buffer : weaponBaseBuffers) {
                    if (buffer) {
                        buffer->Release();
                        buffer = nullptr;
                    }
                }
                for (auto*& buffer : bulletHitBaseBuffers) {
                    if (buffer) {
                        buffer->Release();
                        buffer = nullptr;
                    }
                }
                weaponBank = std::move(updatedWeapons);
                bulletHitBank = std::move(updatedBulletHits);
            }
        }
        ApplyPendingOverrides(
            RuntimeSoundBank::Weapons,
            weaponBank,
            weaponBaseBuffers
        );
        ApplyPendingOverrides(
            RuntimeSoundBank::BulletHits,
            bulletHitBank,
            bulletHitBaseBuffers
        );
        const bool replacementIsEnabled =
            gReplaceOriginal.load(std::memory_order_acquire);
        if (replacementIsEnabled != replacementWasEnabled) {
            if (!replacementIsEnabled) {
                StopAndReleaseVoices(voices);
                minigunSources.clear();
            }
            replacementWasEnabled = replacementIsEnabled;
        }

        auto read = gRead.load(std::memory_order_relaxed);
        const auto write = gWrite.load(std::memory_order_acquire);
        if (!replacementIsEnabled) {
            gRead.store(write, std::memory_order_release);
            CleanupVoices(voices);
            continue;
        }
        const bool isPaused = IsGamePaused();
        if (isPaused) {
            if (!wasPaused) {
                SuspendVoices(voices);
                wasPaused = true;
            }
            // Discard shots accumulated while paused.
            gRead.store(write, std::memory_order_release);
            continue;
        }
        if (wasPaused) {
            ResumeVoices(voices);
            wasPaused = false;
        }
        while (read != write) {
            const auto& job = gJobs[read];
            if (job.type == AudioJobType::BulletHit) {
                ProcessBulletHit(
                    directSound,
                    bulletHitBank,
                    bulletHitBaseBuffers,
                    voices,
                    job
                );
            } else if (job.minigunMode == MinigunAudioMode::None) {
                ProcessGunLayers(
                    directSound,
                    weaponBank,
                    weaponBaseBuffers,
                    voices,
                    job
                );
            } else {
                ProcessMinigunJob(
                    directSound,
                    weaponBank,
                    weaponBaseBuffers,
                    voices,
                    minigunSources,
                    job
                );
            }
            read = (read + 1) & kQueueMask;
        }
        gRead.store(read, std::memory_order_release);
        UpdateMinigunSources(
            directSound,
            weaponBank,
            weaponBaseBuffers,
            voices,
            minigunSources
        );
        CleanupVoices(voices);
        UpdateVoicePositions(voices);
        UpdateVoiceEnvironment(voices);
        RebalanceVoiceMixer(voices);
        StartPendingVoices(voices);
    }

    gReady.store(false, std::memory_order_release);
    StopAndReleaseVoices(voices);
    for (auto*& buffer : weaponBaseBuffers) {
        if (buffer) {
            buffer->Release();
            buffer = nullptr;
        }
    }
    for (auto*& buffer : bulletHitBaseBuffers) {
        if (buffer) {
            buffer->Release();
            buffer = nullptr;
        }
    }
    listener->Release();
    directSound->Release();
    return 0;
}

} // namespace

bool WeaponBackendStart(void* module) {
    gModule = static_cast<HMODULE>(module);
    gStopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    gWakeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!gStopEvent || !gWakeEvent) {
        return false;
    }
    gThread = CreateThread(nullptr, 0, BackendThread, nullptr, 0, nullptr);
    return gThread != nullptr;
}

void WeaponBackendStop() {
    if (gStopEvent) {
        SetEvent(gStopEvent);
    }
}

bool WeaponBackendEnqueue(const AudioJob& job) {
    if (!gReady.load(std::memory_order_acquire)) {
        return false;
    }
    const auto write = gWrite.load(std::memory_order_relaxed);
    const auto next = (write + 1) & kQueueMask;
    if (next == gRead.load(std::memory_order_acquire)) {
        return false;
    }
    gJobs[write] = job;
    gWrite.store(next, std::memory_order_release);
    SetEvent(gWakeEvent);
    return true;
}

bool WeaponBackendShouldReplaceOriginal() {
    return gReady.load(std::memory_order_acquire) &&
           gReplaceOriginal.load(std::memory_order_acquire);
}

void WeaponBackendSetEnabled(bool enabled) {
    gReplaceOriginal.store(enabled, std::memory_order_release);
    if (gWakeEvent) {
        SetEvent(gWakeEvent);
    }
}

bool WeaponBackendIsEnabled() {
    return gReplaceOriginal.load(std::memory_order_acquire);
}

bool WeaponBackendSetSampleOverride(
    RuntimeSoundBank bank,
    std::int16_t soundId,
    const char* path
) {
    const auto bankIndex = static_cast<std::size_t>(bank);
    if (bankIndex >= kRuntimeSoundBankCount ||
        soundId < 0 ||
        static_cast<std::size_t>(soundId) >= kMaxOriginalSounds ||
        !path || !*path) {
        return false;
    }
    const auto soundIndex = static_cast<std::size_t>(soundId);
    AcquireSRWLockExclusive(&gOverrideLock);
    gOverridePaths[bankIndex][soundIndex] = path;
    gOverrideActions[bankIndex][soundIndex] = 1;
    ReleaseSRWLockExclusive(&gOverrideLock);
    if (gWakeEvent) {
        SetEvent(gWakeEvent);
    }
    return true;
}

bool WeaponBackendClearSampleOverride(
    RuntimeSoundBank bank,
    std::int16_t soundId
) {
    const auto bankIndex = static_cast<std::size_t>(bank);
    if (bankIndex >= kRuntimeSoundBankCount ||
        soundId < 0 ||
        static_cast<std::size_t>(soundId) >= kMaxOriginalSounds) {
        return false;
    }
    const auto soundIndex = static_cast<std::size_t>(soundId);
    AcquireSRWLockExclusive(&gOverrideLock);
    gOverridePaths[bankIndex][soundIndex].clear();
    gOverrideActions[bankIndex][soundIndex] = -1;
    ReleaseSRWLockExclusive(&gOverrideLock);
    if (gWakeEvent) {
        SetEvent(gWakeEvent);
    }
    return true;
}

void WeaponBackendSetArchiveOverride(
    const char* archivePath,
    const char* lookupPath
) {
    const std::string nextArchive = archivePath ? archivePath : "";
    const std::string nextLookup = lookupPath ? lookupPath : "";
    AcquireSRWLockExclusive(&gOverrideLock);
    if (gArchiveOverridePath != nextArchive ||
        gLookupOverridePath != nextLookup) {
        gArchiveOverridePath = nextArchive;
        gLookupOverridePath = nextLookup;
        gBankSourcesDirty = true;
    }
    ReleaseSRWLockExclusive(&gOverrideLock);
    if (gWakeEvent) {
        SetEvent(gWakeEvent);
    }
}

void WeaponBackendUpdateCameraTransform(
    const AudioCameraTransform& transform
) {
    static_assert(sizeof(AudioCameraTransform) == sizeof(float) * 12);
    float values[12]{};
    std::memcpy(values, &transform, sizeof(values));
    gCameraTransformGeneration.fetch_add(1, std::memory_order_acq_rel);
    for (std::size_t index = 0; index < std::size(values); ++index) {
        gCameraTransform[index].store(
            values[index],
            std::memory_order_relaxed
        );
    }
    gCameraTransformGeneration.fetch_add(1, std::memory_order_release);
}

void WeaponBackendUpdateEnvironment(bool canSeeOutside) {
    gCanSeeOutside.store(canSeeOutside, std::memory_order_release);
    gEnvironmentFrame.fetch_add(1, std::memory_order_release);
}
