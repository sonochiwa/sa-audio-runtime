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
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::uint32_t kQueueCapacity = 8192;
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
    bool isUnpausable{};
    std::int16_t soundId{-1};
    float headroomDb{};
    AudioVector worldPosition{};
    AudioVector relativePosition{};
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
    std::uintptr_t vehicleSourceKey{};
    bool isVehicleOneShot{};
    std::uint32_t vehicleGeneration{};
    std::uint32_t sampleRate{};
    float playbackSpeed{1.0f};
    float dopplerScale{};
    std::int16_t vehicleBankId{-1};
    bool vehicleLoopPending{};
    bool isRuntimeEffect{};
    bool isStatefulEffect{};
    bool reportsCompletion{};
};

struct MinigunSource {
    std::uintptr_t key{};
    ULONGLONG lastHeartbeat{};
    AudioJob job{};
};

struct VehicleSource {
    AudioJob job{};
    ULONGLONG lastHeartbeat{};
    std::uint32_t lastStartedGeneration{};
};

struct VirtualRuntimeSource {
    AudioJob job{};
    ULONGLONG lastUpdateAt{};
    DWORD durationMs{};
    double playPositionMs{};
    bool looping{};
};

enum class VoiceGroup : std::uint8_t {
    Weapons,
    Vehicles,
    Runtime
};

struct VehicleBank {
    OriginalSoundBank samples;
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds> baseBuffers{};
    bool loadAttempted{};
    bool loaded{};
};

SRWLOCK gOverrideLock = SRWLOCK_INIT;
std::unordered_map<std::uint32_t, std::string> gDynamicOverridePaths;
std::array<std::string, 9> gPackOverridePaths{};

std::uint32_t GetDynamicOverrideKey(
    std::int16_t bankId,
    std::int16_t soundId
) {
    return
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(bankId)) << 16 |
        static_cast<std::uint16_t>(soundId);
}

int GetPackIdForBank(std::int16_t bankId) {
    constexpr std::int16_t firstBanks[] = {
        0, 7, 144, 147, 365, 411, 429, 638, 690
    };
    int packId = -1;
    for (int index = 0; index < static_cast<int>(std::size(firstBanks));
         ++index) {
        if (bankId < firstBanks[index]) {
            break;
        }
        packId = index;
    }
    return packId;
}

const char* GetPackName(int packId) {
    constexpr const char* names[] = {
        "FEET", "GENRL", "PAIN_A", "SCRIPT", "SPC_EA",
        "SPC_FA", "SPC_GA", "SPC_NA", "SPC_PA"
    };
    return packId >= 0 && packId < static_cast<int>(std::size(names))
        ? names[packId]
        : nullptr;
}

void ApplyDynamicOverrides(std::int16_t bankId, OriginalSoundBank& bank) {
    AcquireSRWLockShared(&gOverrideLock);
    for (std::int16_t soundId = 0;
         soundId < static_cast<std::int16_t>(kMaxOriginalSounds);
         ++soundId) {
        const auto found = gDynamicOverridePaths.find(
            GetDynamicOverrideKey(bankId, soundId)
        );
        if (found == gDynamicOverridePaths.end()) {
            continue;
        }
        std::string error;
        bank.ApplyWaveOverride(soundId, found->second, error);
    }
    ReleaseSRWLockShared(&gOverrideLock);
}

void ReleaseVehicleBanks(
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks
) {
    for (auto& [bankId, bank] : banks) {
        if (!bank) {
            continue;
        }
        for (auto*& buffer : bank->baseBuffers) {
            if (buffer) {
                buffer->Release();
                buffer = nullptr;
            }
        }
    }
    banks.clear();
}

HMODULE gModule{};
HANDLE gThread{};
HANDLE gWakeEvent{};
HANDLE gStopEvent{};
std::array<AudioJob, kQueueCapacity> gJobs{};
std::atomic<std::uint32_t> gWrite{};
std::atomic<std::uint32_t> gRead{};
std::atomic<bool> gReady{};
std::atomic<bool> gReplaceOriginal{};
std::atomic<bool> gReplaceVehicles{};
std::atomic<bool> gReplaceDialogues{};
std::atomic<bool> gDeviceRecoveryRequested{};
SRWLOCK gCoalescedJobLock = SRWLOCK_INIT;
std::vector<AudioJob> gCoalescedJobs;
SRWLOCK gCompletionLock = SRWLOCK_INIT;
std::deque<AudioCompletion> gCompletions;
std::array<std::atomic<float>, 12> gCameraTransform{};
std::atomic<std::uint32_t> gCameraTransformGeneration{};
std::atomic<bool> gCanSeeOutside{true};
std::atomic<std::uint32_t> gEnvironmentFrame{};
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
bool gDynamicBanksDirty{};

void RequestDeviceRecovery() {
    gDeviceRecoveryRequested.store(true, std::memory_order_release);
}

bool AudioCallSucceeded(HRESULT result) {
    if (SUCCEEDED(result)) {
        return true;
    }
    RequestDeviceRecovery();
    return false;
}

bool IsCoalescedSourceJob(AudioJobType type) {
    return type == AudioJobType::VehicleUpdate ||
           type == AudioJobType::VehicleStop ||
           type == AudioJobType::DialogueUpdate ||
           type == AudioJobType::DialogueStop ||
           type == AudioJobType::StatefulUpdate ||
           type == AudioJobType::StatefulStop;
}

std::uint64_t GetCoalescedJobKey(const AudioJob& job) {
    std::uint64_t group{};
    switch (job.type) {
    case AudioJobType::VehicleUpdate:
    case AudioJobType::VehicleStop:
        group = 1;
        break;
    case AudioJobType::DialogueUpdate:
    case AudioJobType::DialogueStop:
        group = 2;
        break;
    default:
        group = 3;
        break;
    }
    return (group << 56) |
           static_cast<std::uint64_t>(job.sourceKey);
}

void TakeCoalescedJobs(std::vector<AudioJob>& jobs) {
    AcquireSRWLockExclusive(&gCoalescedJobLock);
    jobs.assign(gCoalescedJobs.begin(), gCoalescedJobs.end());
    gCoalescedJobs.clear();
    ReleaseSRWLockExclusive(&gCoalescedJobLock);
}

void ClearCoalescedJobs() {
    AcquireSRWLockExclusive(&gCoalescedJobLock);
    gCoalescedJobs.clear();
    ReleaseSRWLockExclusive(&gCoalescedJobLock);
}

void QueueCompletion(const AudioCompletion& completion) {
    constexpr std::size_t kMaximumCompletionBacklog = 2048;
    AcquireSRWLockExclusive(&gCompletionLock);
    if (!completion.finished &&
        gCompletions.size() >= kMaximumCompletionBacklog) {
        const auto existing = std::find_if(
            gCompletions.begin(),
            gCompletions.end(),
            [&](const AudioCompletion& queued) {
                return !queued.finished &&
                       queued.sourceKey == completion.sourceKey &&
                       queued.sourceGeneration ==
                           completion.sourceGeneration;
            }
        );
        if (existing != gCompletions.end()) {
            *existing = completion;
        }
        ReleaseSRWLockExclusive(&gCompletionLock);
        return;
    }
    gCompletions.push_back(completion);
    ReleaseSRWLockExclusive(&gCompletionLock);
}

std::string GetModuleDirectory() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(gModule, path, MAX_PATH);
    if (auto* slash = std::strrchr(path, '\\')) {
        *slash = '\0';
    }
    return path;
}

std::string GetGameDirectory() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (auto* slash = std::strrchr(path, '\\')) {
        *slash = '\0';
    }
    return path;
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

bool TakeDynamicBankUpdate() {
    AcquireSRWLockExclusive(&gOverrideLock);
    const bool changed = gDynamicBanksDirty;
    gDynamicBanksDirty = false;
    ReleaseSRWLockExclusive(&gOverrideLock);
    return changed;
}

void PublishCompletion(const Voice& voice) {
    if (!voice.reportsCompletion || voice.vehicleSourceKey == 0) {
        return;
    }
    QueueCompletion({
        voice.vehicleSourceKey,
        voice.vehicleGeneration
    });
}

void PublishDialogueStarted(
    const AudioJob& job,
    const OriginalPcmSample& sample
) {
    const auto samples = sample.pcm.size() / sizeof(std::int16_t);
    const auto length = sample.sampleRate != 0
        ? static_cast<double>(samples) * 1000.0 /
            static_cast<double>(sample.sampleRate) /
            std::max(static_cast<double>(job.baseSpeed), 0.05)
        : 0.0;
    QueueCompletion({
        job.sourceKey,
        job.sourceGeneration,
        false,
        static_cast<std::int16_t>(std::clamp(length, 0.0, 32767.0))
    });
}

void PublishCompletion(const AudioJob& job) {
    Voice voice{};
    voice.reportsCompletion = true;
    voice.vehicleSourceKey = job.sourceKey;
    voice.vehicleGeneration = job.sourceGeneration;
    PublishCompletion(voice);
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
    std::uintptr_t minigunSourceKey = 0,
    bool looping = false,
    bool isBulletHit = false
) {
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
    bool publishCompletions = true
) {
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
        const auto finalVolume = static_cast<LONG>(
            std::clamp(
                std::min(voice.mixVolumeDb, 0.0f) +
                    voice.outputGainDb +
                    compressionGainDb,
                -100.0f,
                0.0f
            ) * 100.0f
        );
        AudioCallSucceeded(voice.buffer->SetVolume(finalVolume));
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

VehicleBank* GetVehicleBank(
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    const std::string& lookupPath,
    const std::string& archivePath,
    std::int16_t bankId
) {
    if (bankId < 0) {
        return nullptr;
    }
    auto& entry = banks[bankId];
    if (!entry) {
        entry = std::make_unique<VehicleBank>();
    }
    if (!entry->loadAttempted) {
        entry->loadAttempted = true;
        std::string error;
        const auto thread = GetCurrentThread();
        const auto previousPriority = GetThreadPriority(thread);
        if (previousPriority != THREAD_PRIORITY_ERROR_RETURN) {
            SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        }
        entry->loaded = entry->samples.Load(
            lookupPath,
            archivePath,
            static_cast<std::size_t>(bankId),
            error
        );
        if (entry->loaded) {
            ApplyDynamicOverrides(bankId, entry->samples);
        }
        if (previousPriority != THREAD_PRIORITY_ERROR_RETURN) {
            SetThreadPriority(thread, previousPriority);
        }
    }
    return entry->loaded ? entry.get() : nullptr;
}

VehicleBank* GetDialogueBank(
    std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks,
    const std::string& gameDirectory,
    const std::string& lookupPath,
    std::int16_t bankId
) {
    if (bankId < 0) {
        return nullptr;
    }
    auto& entry = banks[bankId];
    if (!entry) {
        entry = std::make_unique<VehicleBank>();
    }
    if (!entry->loadAttempted) {
        entry->loadAttempted = true;
        std::string error;
        const auto thread = GetCurrentThread();
        const auto previousPriority = GetThreadPriority(thread);
        if (previousPriority != THREAD_PRIORITY_ERROR_RETURN) {
            SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        }
        const auto packId = GetPackIdForBank(bankId);
        const auto* packName = GetPackName(packId);
        std::string archivePath;
        AcquireSRWLockShared(&gOverrideLock);
        if (packId == 1 && !gArchiveOverridePath.empty()) {
            archivePath = gArchiveOverridePath;
        } else if (packId >= 0 &&
            packId < static_cast<int>(gPackOverridePaths.size())) {
            archivePath = gPackOverridePaths[packId];
        }
        ReleaseSRWLockShared(&gOverrideLock);
        if (archivePath.empty() && packName) {
            archivePath = gameDirectory + "\\audio\\SFX\\" + packName;
        }
        entry->loaded = !archivePath.empty() && entry->samples.Load(
            lookupPath,
            archivePath,
            static_cast<std::size_t>(bankId),
            error
        );
        if (entry->loaded) {
            ApplyDynamicOverrides(bankId, entry->samples);
        }
        if (previousPriority != THREAD_PRIORITY_ERROR_RETURN) {
            SetThreadPriority(thread, previousPriority);
        }
    }
    return entry->loaded ? entry.get() : nullptr;
}

Voice* FindVehicleVoice(
    std::vector<Voice>& voices,
    std::uintptr_t sourceKey
) {
    const auto found = std::find_if(
        voices.begin(),
        voices.end(),
        [sourceKey](const Voice& voice) {
            return voice.vehicleSourceKey == sourceKey;
        }
    );
    return found != voices.end() ? &*found : nullptr;
}

std::vector<Voice>::iterator FindQuietestStatefulVoice(
    std::vector<Voice>& voices
) {
    auto quietest = voices.end();
    for (auto voice = voices.begin(); voice != voices.end(); ++voice) {
        if (!voice->isStatefulEffect) {
            continue;
        }
        if (quietest == voices.end() ||
            voice->mixVolumeDb < quietest->mixVolumeDb) {
            quietest = voice;
        }
    }
    return quietest;
}

void StopVehicleVoice(
    std::vector<Voice>& voices,
    std::uintptr_t sourceKey
) {
    if (auto* voice = FindVehicleVoice(voices, sourceKey);
        voice && voice->buffer) {
        voice->buffer->Stop();
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
    if (!AudioCallSucceeded(buffer->SetFrequency(frequency)) ||
        !AudioCallSucceeded(buffer->SetVolume(static_cast<LONG>(
            std::clamp(listenerVolume, -100.0f, 0.0f) * 100.0f
        )))) {
        spatialBuffer->Release();
        buffer->Release();
        return false;
    }
    if (job.startPercentage && job.playTime > 0) {
        DSBCAPS capabilities{};
        capabilities.dwSize = sizeof(capabilities);
        if (SUCCEEDED(buffer->GetCaps(&capabilities))) {
            auto position = static_cast<DWORD>(
                static_cast<std::uint64_t>(capabilities.dwBufferBytes) *
                static_cast<std::uint16_t>(job.playTime) /
                100u
            );
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
    voice.looping = hasLoop && !delayedLoop;
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
    voice.isRuntimeEffect = runtimeVoice;
    voice.isStatefulEffect =
        job.type == AudioJobType::StatefulStart;
    voice.reportsCompletion =
        job.type == AudioJobType::DialogueStart ||
        job.type == AudioJobType::StatefulStart;
    voices.push_back(voice);
    return true;
}

void ContinueVehicleLoops(
    IDirectSound8* directSound,
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
        if (status & DSBSTATUS_PLAYING) {
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
        if (!sample || sample->loopStartSample <= 0) {
            voice.vehicleLoopPending = false;
            continue;
        }
        const auto loopByte =
            static_cast<std::size_t>(sample->loopStartSample) * 2;
        if (loopByte >= sample->pcm.size()) {
            voice.vehicleLoopPending = false;
            continue;
        }
        OriginalPcmSample loopSample = *sample;
        loopSample.pcm.assign(
            sample->pcm.begin() + loopByte,
            sample->pcm.end()
        );
        loopSample.loopStartSample = 0;
        IDirectSoundBuffer* loopBuffer{};
        if (!CreateBaseBuffer(directSound, loopSample, &loopBuffer)) {
            voice.vehicleLoopPending = false;
            continue;
        }
        IDirectSound3DBuffer* loopSpatial{};
        if (FAILED(loopBuffer->QueryInterface(
                IID_IDirectSound3DBuffer,
                reinterpret_cast<void**>(&loopSpatial)
            )) || !loopSpatial) {
            loopBuffer->Release();
            voice.vehicleLoopPending = false;
            continue;
        }
        voice.spatialBuffer->Release();
        voice.buffer->Release();
        voice.buffer = loopBuffer;
        voice.spatialBuffer = loopSpatial;
        voice.looping = true;
        voice.pendingStart = true;
        voice.vehicleLoopPending = false;
        const auto frequency = static_cast<DWORD>(std::clamp(
            static_cast<double>(voice.sampleRate) *
                std::max(
                    voice.playbackSpeed * voice.dopplerScale,
                    0.05f
                ),
            static_cast<double>(DSBFREQUENCY_MIN),
            static_cast<double>(DSBFREQUENCY_MAX)
        ));
        if (!AudioCallSucceeded(loopBuffer->SetFrequency(frequency)) ||
            !AudioCallSucceeded(loopSpatial->SetMode(
                voice.isFrontEnd
                    ? DS3DMODE_HEADRELATIVE
                    : DS3DMODE_NORMAL,
                DS3D_IMMEDIATE
            )) ||
            !AudioCallSucceeded(loopSpatial->SetMinDistance(
                1.0f,
                DS3D_IMMEDIATE
            )) ||
            !AudioCallSucceeded(loopSpatial->SetMaxDistance(
                10000.0f,
                DS3D_IMMEDIATE
            )) ||
            !AudioCallSucceeded(loopSpatial->SetPosition(
                voice.worldPosition.x,
                voice.isFrontEnd && voice.worldPosition.y == 0.0f
                    ? 1.0f
                    : voice.worldPosition.y,
                voice.worldPosition.z,
                DS3D_IMMEDIATE
            ))) {
            voice.vehicleLoopPending = false;
        }
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
        if (voice && voice->buffer) {
            voice->buffer->Stop();
            CleanupVoices(voices);
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
        }
    }
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

    if (FAILED(listener->SetPosition(
            0.0f,
            0.0f,
            0.0f,
            DS3D_IMMEDIATE
        )) ||
        FAILED(listener->SetOrientation(
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            -1.0f,
            DS3D_IMMEDIATE
        )) ||
        FAILED(listener->SetRolloffFactor(0.0f, DS3D_IMMEDIATE)) ||
        FAILED(listener->SetDopplerFactor(0.0f, DS3D_IMMEDIATE))) {
        listener->Release();
        return false;
    }
    *output = listener;
    return true;
}

bool CreateAudioDevice(
    IDirectSound8** directSoundOutput,
    IDirectSound3DListener** listenerOutput
) {
    auto* window = FindProcessWindow();
    if (!window) {
        return false;
    }

    IDirectSound8* directSound{};
    if (FAILED(DirectSoundCreate8(nullptr, &directSound, nullptr))) {
        return false;
    }
    if (FAILED(directSound->SetCooperativeLevel(
            window,
            DSSCL_NORMAL
        ))) {
        directSound->Release();
        return false;
    }

    IDirectSound3DListener* listener{};
    if (!InitialiseListener(directSound, &listener)) {
        directSound->Release();
        return false;
    }

    *directSoundOutput = directSound;
    *listenerOutput = listener;
    return true;
}

bool IsAudioDeviceHealthy(IDirectSound8* directSound) {
    if (!directSound) {
        return false;
    }
    DSCAPS capabilities{};
    capabilities.dwSize = sizeof(capabilities);
    return SUCCEEDED(directSound->GetCaps(&capabilities));
}

void ReleaseBaseBuffers(
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& buffers
) {
    for (auto*& buffer : buffers) {
        if (buffer) {
            buffer->Release();
            buffer = nullptr;
        }
    }
}

DWORD WINAPI BackendThread(void*) {
    auto weaponBankStorage = std::make_unique<OriginalSoundBank>();
    auto bulletHitBankStorage = std::make_unique<OriginalSoundBank>();
    auto& weaponBank = *weaponBankStorage;
    auto& bulletHitBank = *bulletHitBankStorage;
    std::string error;
    const auto gameDirectory = GetGameDirectory();
    std::string activeArchivePath = gameDirectory + "\\audio\\SFX\\GENRL";
    std::string activeLookupPath =
        gameDirectory + "\\audio\\CONFIG\\BankLkup.dat";
    if (!weaponBank.Load(gameDirectory, kWeaponBankId, error) ||
        !bulletHitBank.Load(gameDirectory, kBulletHitBankId, error)) {
        return 1;
    }

    IDirectSound8* directSound{};
    IDirectSound3DListener* listener{};
    for (int attempt = 0;
         attempt < 100 &&
         !CreateAudioDevice(&directSound, &listener);
         ++attempt) {
        if (WaitForSingleObject(gStopEvent, 50) == WAIT_OBJECT_0) {
            return 2;
        }
    }
    if (!directSound || !listener) {
        return 3;
    }

    std::array<IDirectSoundBuffer*, kMaxOriginalSounds> weaponBaseBuffers{};
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds> bulletHitBaseBuffers{};
    std::vector<Voice> voices;
    std::vector<MinigunSource> minigunSources;
    std::vector<AudioJob> coalescedJobs;
    std::unordered_map<std::uintptr_t, VehicleSource> vehicleSources;
    std::unordered_map<std::uintptr_t, VirtualRuntimeSource>
        virtualRuntimeSources;
    std::unordered_map<
        std::int16_t,
        std::unique_ptr<VehicleBank>
    > vehicleBanks;
    voices.reserve(128);
    minigunSources.reserve(16);
    coalescedJobs.reserve(1024);
    vehicleSources.reserve(256);
    virtualRuntimeSources.reserve(128);
    vehicleBanks.reserve(64);
    gReady.store(true, std::memory_order_release);

    HANDLE waits[] = {gStopEvent, gWakeEvent};
    bool wasPaused{};
    bool replacementWasEnabled =
        gReplaceOriginal.load(std::memory_order_acquire);
    bool vehicleReplacementWasEnabled =
        gReplaceVehicles.load(std::memory_order_acquire);
    bool dialogueReplacementWasEnabled =
        gReplaceDialogues.load(std::memory_order_acquire);
    auto nextDeviceHealthCheck = GetTickCount64() + 1000;
    while (WaitForMultipleObjects(2, waits, FALSE, 10) != WAIT_OBJECT_0) {
        const auto deviceCheckTime = GetTickCount64();
        const bool recoveryRequested =
            gDeviceRecoveryRequested.exchange(
                false,
                std::memory_order_acq_rel
            );
        if (recoveryRequested ||
            deviceCheckTime >= nextDeviceHealthCheck) {
            nextDeviceHealthCheck = deviceCheckTime + 1000;
            if (recoveryRequested ||
                !IsAudioDeviceHealthy(directSound)) {
                gReady.store(false, std::memory_order_release);
                VirtualizeRuntimeVoices(
                    voices,
                    virtualRuntimeSources,
                    true
                );
                StopAndReleaseVoices(voices, false);
                minigunSources.clear();
                ReleaseBaseBuffers(weaponBaseBuffers);
                ReleaseBaseBuffers(bulletHitBaseBuffers);
                ReleaseVehicleBanks(vehicleBanks);
                if (listener) {
                    listener->Release();
                    listener = nullptr;
                }
                if (directSound) {
                    directSound->Release();
                    directSound = nullptr;
                }

                while (WaitForSingleObject(gStopEvent, 500) !=
                       WAIT_OBJECT_0) {
                    if (CreateAudioDevice(&directSound, &listener)) {
                        gReady.store(true, std::memory_order_release);
                        gDeviceRecoveryRequested.store(
                            false,
                            std::memory_order_release
                        );
                        nextDeviceHealthCheck =
                            GetTickCount64() + 1000;
                        break;
                    }
                }
                if (!directSound || !listener) {
                    break;
                }
            }
        }
        std::string archiveOverride;
        std::string lookupOverride;
        if (TakeBankSourceUpdate(archiveOverride, lookupOverride)) {
            const auto requestedArchivePath = archiveOverride.empty()
                ? gameDirectory + "\\audio\\SFX\\GENRL"
                : archiveOverride;
            const auto requestedLookupPath = lookupOverride.empty()
                ? gameDirectory + "\\audio\\CONFIG\\BankLkup.dat"
                : lookupOverride;
            auto updatedWeapons =
                std::make_unique<OriginalSoundBank>();
            auto updatedBulletHits =
                std::make_unique<OriginalSoundBank>();
            error.clear();
            if (updatedWeapons->Load(
                    requestedLookupPath,
                    requestedArchivePath,
                    kWeaponBankId,
                    error
                ) &&
                updatedBulletHits->Load(
                    requestedLookupPath,
                    requestedArchivePath,
                    kBulletHitBankId,
                    error
                )) {
                ApplyCurrentOverrides(
                    RuntimeSoundBank::Weapons,
                    *updatedWeapons
                );
                ApplyCurrentOverrides(
                    RuntimeSoundBank::BulletHits,
                    *updatedBulletHits
                );
                StopAndReleaseVoices(voices);
                minigunSources.clear();
                ReleaseBaseBuffers(weaponBaseBuffers);
                ReleaseBaseBuffers(bulletHitBaseBuffers);
                weaponBank = std::move(*updatedWeapons);
                bulletHitBank = std::move(*updatedBulletHits);
                activeArchivePath = requestedArchivePath;
                activeLookupPath = requestedLookupPath;
                ReleaseVehicleBanks(vehicleBanks);
            }
        }
        if (TakeDynamicBankUpdate()) {
            ReleaseVehicleBanks(vehicleBanks);
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
                StopVoicesByOwner(voices, 0);
                minigunSources.clear();
            }
            replacementWasEnabled = replacementIsEnabled;
        }
        const bool vehicleReplacementIsEnabled =
            gReplaceVehicles.load(std::memory_order_acquire);
        if (vehicleReplacementIsEnabled != vehicleReplacementWasEnabled) {
            if (!vehicleReplacementIsEnabled) {
                StopVoicesByOwner(voices, 1);
                vehicleSources.clear();
            }
            vehicleReplacementWasEnabled = vehicleReplacementIsEnabled;
        }
        const bool dialogueReplacementIsEnabled =
            gReplaceDialogues.load(std::memory_order_acquire);
        if (dialogueReplacementIsEnabled != dialogueReplacementWasEnabled) {
            if (!dialogueReplacementIsEnabled) {
                StopVoicesByOwner(voices, 2);
                virtualRuntimeSources.clear();
            }
            dialogueReplacementWasEnabled = dialogueReplacementIsEnabled;
        }

        auto read = gRead.load(std::memory_order_relaxed);
        const auto write = gWrite.load(std::memory_order_acquire);
        if (!replacementIsEnabled &&
            !vehicleReplacementIsEnabled &&
            !dialogueReplacementIsEnabled) {
            gRead.store(write, std::memory_order_release);
            ClearCoalescedJobs();
            CleanupVoices(voices);
            continue;
        }
        const bool isPaused = IsGamePaused();
        if (isPaused) {
            if (!wasPaused) {
                SuspendVoices(voices);
                wasPaused = true;
            }
            TakeCoalescedJobs(coalescedJobs);
            for (const auto& job : coalescedJobs) {
                if (job.type == AudioJobType::VehicleStop) {
                    ProcessVehicleJob(vehicleSources, voices, job);
                } else if (job.type == AudioJobType::DialogueStop ||
                           job.type == AudioJobType::StatefulStop) {
                    ProcessDialogueJob(
                        directSound,
                        gameDirectory,
                        activeLookupPath,
                        vehicleBanks,
                        voices,
                        virtualRuntimeSources,
                        job
                    );
                }
            }
            const auto pausedAt = GetTickCount64();
            for (auto& [key, source] : virtualRuntimeSources) {
                source.lastUpdateAt = pausedAt;
            }
            while (read != write) {
                const auto& job = gJobs[read];
                if (dialogueReplacementIsEnabled &&
                    (job.type == AudioJobType::DialogueStart ||
                     job.type == AudioJobType::StatefulStart)) {
                    ProcessDialogueJob(
                        directSound,
                        gameDirectory,
                        activeLookupPath,
                        vehicleBanks,
                        voices,
                        virtualRuntimeSources,
                        job
                    );
                }
                read = (read + 1) & kQueueMask;
            }
            gRead.store(write, std::memory_order_release);
            continue;
        }
        if (wasPaused) {
            ResumeVoices(voices);
            const auto resumedAt = GetTickCount64();
            for (auto& [key, source] : virtualRuntimeSources) {
                source.lastUpdateAt = resumedAt;
            }
            wasPaused = false;
        }
        TakeCoalescedJobs(coalescedJobs);
        for (const auto& job : coalescedJobs) {
            if (job.type == AudioJobType::VehicleUpdate ||
                job.type == AudioJobType::VehicleStop) {
                if (vehicleReplacementIsEnabled) {
                    ProcessVehicleJob(vehicleSources, voices, job);
                }
            } else if (dialogueReplacementIsEnabled) {
                ProcessDialogueJob(
                    directSound,
                    gameDirectory,
                    activeLookupPath,
                    vehicleBanks,
                    voices,
                    virtualRuntimeSources,
                    job
                );
            }
        }
        while (read != write) {
            const auto& job = gJobs[read];
            if (job.type == AudioJobType::DialogueStart ||
                job.type == AudioJobType::DialogueUpdate ||
                job.type == AudioJobType::DialogueStop ||
                job.type == AudioJobType::StatefulStart ||
                job.type == AudioJobType::StatefulUpdate ||
                job.type == AudioJobType::StatefulStop ||
                job.type == AudioJobType::GenericOneShot) {
                if (dialogueReplacementIsEnabled) {
                    ProcessDialogueJob(
                        directSound,
                        gameDirectory,
                        activeLookupPath,
                        vehicleBanks,
                        voices,
                        virtualRuntimeSources,
                        job
                    );
                }
            } else if (job.type == AudioJobType::VehicleOneShot) {
                if (vehicleReplacementIsEnabled) {
                    ProcessVehicleOneShot(
                        directSound,
                        activeLookupPath,
                        activeArchivePath,
                        vehicleBanks,
                        voices,
                        job
                    );
                }
            } else if (job.type == AudioJobType::VehicleUpdate ||
                       job.type == AudioJobType::VehicleStop) {
                if (vehicleReplacementIsEnabled) {
                    ProcessVehicleJob(vehicleSources, voices, job);
                }
            } else if (!replacementIsEnabled) {
                read = (read + 1) & kQueueMask;
                continue;
            } else if (job.type == AudioJobType::BulletHit) {
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
        if (vehicleReplacementIsEnabled) {
            ContinueVehicleLoops(
                directSound,
                activeLookupPath,
                activeArchivePath,
                vehicleBanks,
                voices
            );
        }
        CleanupVoices(voices);
        if (replacementIsEnabled) {
            UpdateMinigunSources(
                directSound,
                weaponBank,
                weaponBaseBuffers,
                voices,
                minigunSources
            );
        }
        if (vehicleReplacementIsEnabled) {
            UpdateVehicleSources(
                directSound,
                activeLookupPath,
                activeArchivePath,
                vehicleBanks,
                vehicleSources,
                voices
            );
        }
        if (dialogueReplacementIsEnabled) {
            UpdateVirtualRuntimeSources(
                directSound,
                gameDirectory,
                activeLookupPath,
                vehicleBanks,
                voices,
                virtualRuntimeSources
            );
        }
        UpdateVoicePositions(voices);
        UpdateVoiceEnvironment(voices);
        if (dialogueReplacementIsEnabled) {
            VirtualizeRuntimeVoices(
                voices,
                virtualRuntimeSources,
                false
            );
        }
        RebalanceVoiceMixer(voices);
        StartPendingVoices(voices);
    }

    gReady.store(false, std::memory_order_release);
    StopAndReleaseVoices(voices);
    ReleaseBaseBuffers(weaponBaseBuffers);
    ReleaseBaseBuffers(bulletHitBaseBuffers);
    ReleaseVehicleBanks(vehicleBanks);
    if (listener) {
        listener->Release();
    }
    if (directSound) {
        directSound->Release();
    }
    return 0;
}

} // namespace

bool WeaponBackendStart(void* module) {
    gModule = static_cast<HMODULE>(module);
    gReady.store(false, std::memory_order_release);
    gDeviceRecoveryRequested.store(false, std::memory_order_release);
    gWrite.store(0, std::memory_order_release);
    gRead.store(0, std::memory_order_release);
    ClearCoalescedJobs();
    AcquireSRWLockExclusive(&gCoalescedJobLock);
    gCoalescedJobs.reserve(1024);
    ReleaseSRWLockExclusive(&gCoalescedJobLock);
    AcquireSRWLockExclusive(&gCompletionLock);
    gCompletions.clear();
    ReleaseSRWLockExclusive(&gCompletionLock);
    gStopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    gWakeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!gStopEvent || !gWakeEvent) {
        if (gWakeEvent) {
            CloseHandle(gWakeEvent);
            gWakeEvent = nullptr;
        }
        if (gStopEvent) {
            CloseHandle(gStopEvent);
            gStopEvent = nullptr;
        }
        return false;
    }
    gThread = CreateThread(nullptr, 0, BackendThread, nullptr, 0, nullptr);
    if (!gThread) {
        CloseHandle(gWakeEvent);
        CloseHandle(gStopEvent);
        gWakeEvent = nullptr;
        gStopEvent = nullptr;
        return false;
    }
    return true;
}

void WeaponBackendStop() {
    gReady.store(false, std::memory_order_release);
    if (gStopEvent) {
        SetEvent(gStopEvent);
    }
}

bool WeaponBackendEnqueue(const AudioJob& job) {
    if (!gReady.load(std::memory_order_acquire)) {
        return false;
    }
    if (IsCoalescedSourceJob(job.type)) {
        AcquireSRWLockExclusive(&gCoalescedJobLock);
        const auto key = GetCoalescedJobKey(job);
        const auto existing = std::find_if(
            gCoalescedJobs.begin(),
            gCoalescedJobs.end(),
            [&](const AudioJob& queued) {
                return GetCoalescedJobKey(queued) == key;
            }
        );
        if (existing != gCoalescedJobs.end()) {
            *existing = job;
        } else if (gCoalescedJobs.size() < 1024) {
            gCoalescedJobs.push_back(job);
        } else if (job.type == AudioJobType::VehicleStop ||
                   job.type == AudioJobType::DialogueStop ||
                   job.type == AudioJobType::StatefulStop) {
            const auto update = std::find_if(
                gCoalescedJobs.begin(),
                gCoalescedJobs.end(),
                [](const AudioJob& queued) {
                    return queued.type == AudioJobType::VehicleUpdate ||
                           queued.type == AudioJobType::DialogueUpdate ||
                           queued.type == AudioJobType::StatefulUpdate;
                }
            );
            if (update != gCoalescedJobs.end()) {
                *update = job;
            } else {
                ReleaseSRWLockExclusive(&gCoalescedJobLock);
                return false;
            }
        } else {
            ReleaseSRWLockExclusive(&gCoalescedJobLock);
            return false;
        }
        ReleaseSRWLockExclusive(&gCoalescedJobLock);
        SetEvent(gWakeEvent);
        return true;
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

bool VehicleBackendShouldReplaceOriginal() {
    return gReady.load(std::memory_order_acquire) &&
           gReplaceVehicles.load(std::memory_order_acquire);
}

void VehicleBackendSetEnabled(bool enabled) {
    gReplaceVehicles.store(enabled, std::memory_order_release);
    if (gWakeEvent) {
        SetEvent(gWakeEvent);
    }
}

bool VehicleBackendEnqueue(const AudioJob& job) {
    return WeaponBackendEnqueue(job);
}

bool DialogueBackendShouldReplaceOriginal() {
    return gReady.load(std::memory_order_acquire) &&
           gReplaceDialogues.load(std::memory_order_acquire);
}

void DialogueBackendSetEnabled(bool enabled) {
    gReplaceDialogues.store(enabled, std::memory_order_release);
    if (gWakeEvent) {
        SetEvent(gWakeEvent);
    }
}

bool DialogueBackendEnqueue(const AudioJob& job) {
    return WeaponBackendEnqueue(job);
}

bool DialogueBackendPollCompletion(AudioCompletion& completion) {
    AcquireSRWLockExclusive(&gCompletionLock);
    if (gCompletions.empty()) {
        ReleaseSRWLockExclusive(&gCompletionLock);
        return false;
    }
    completion = gCompletions.front();
    gCompletions.pop_front();
    ReleaseSRWLockExclusive(&gCompletionLock);
    return true;
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
    bool changed{};
    AcquireSRWLockExclusive(&gOverrideLock);
    if (gOverridePaths[bankIndex][soundIndex] != path ||
        gOverrideActions[bankIndex][soundIndex] != 1) {
        gOverridePaths[bankIndex][soundIndex] = path;
        gOverrideActions[bankIndex][soundIndex] = 1;
        changed = true;
    }
    ReleaseSRWLockExclusive(&gOverrideLock);
    if (changed && gWakeEvent) {
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
    bool changed{};
    AcquireSRWLockExclusive(&gOverrideLock);
    if (!gOverridePaths[bankIndex][soundIndex].empty() ||
        gOverrideActions[bankIndex][soundIndex] != -1) {
        gOverridePaths[bankIndex][soundIndex].clear();
        gOverrideActions[bankIndex][soundIndex] = -1;
        changed = true;
    }
    ReleaseSRWLockExclusive(&gOverrideLock);
    if (changed && gWakeEvent) {
        SetEvent(gWakeEvent);
    }
    return true;
}

bool WeaponBackendSetDynamicSampleOverride(
    std::int16_t bankId,
    std::int16_t soundId,
    const char* path,
    bool installed
) {
    if (bankId < 0 || soundId < 0 ||
        static_cast<std::size_t>(soundId) >= kMaxOriginalSounds ||
        (installed && (!path || !*path))) {
        return false;
    }
    bool changed{};
    AcquireSRWLockExclusive(&gOverrideLock);
    const auto key = GetDynamicOverrideKey(bankId, soundId);
    if (installed) {
        const auto found = gDynamicOverridePaths.find(key);
        if (found == gDynamicOverridePaths.end() || found->second != path) {
            gDynamicOverridePaths[key] = path;
            changed = true;
        }
    } else {
        changed = gDynamicOverridePaths.erase(key) != 0;
    }
    if (changed) {
        gDynamicBanksDirty = true;
    }
    ReleaseSRWLockExclusive(&gOverrideLock);
    if (changed && gWakeEvent) {
        SetEvent(gWakeEvent);
    }
    return true;
}

bool WeaponBackendSetPackOverride(
    std::int32_t packId,
    const char* path,
    bool installed
) {
    if (packId < 0 ||
        packId >= static_cast<std::int32_t>(gPackOverridePaths.size()) ||
        (installed && (!path || !*path))) {
        return false;
    }
    const auto nextPath = installed ? std::string(path) : std::string{};
    bool changed{};
    AcquireSRWLockExclusive(&gOverrideLock);
    auto& currentPath = gPackOverridePaths[static_cast<std::size_t>(packId)];
    if (currentPath != nextPath) {
        currentPath = nextPath;
        gDynamicBanksDirty = true;
        changed = true;
    }
    ReleaseSRWLockExclusive(&gOverrideLock);
    if (changed && gWakeEvent) {
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
    bool changed{};
    AcquireSRWLockExclusive(&gOverrideLock);
    if (gArchiveOverridePath != nextArchive ||
        gLookupOverridePath != nextLookup) {
        gArchiveOverridePath = nextArchive;
        gLookupOverridePath = nextLookup;
        gBankSourcesDirty = true;
        changed = true;
    }
    ReleaseSRWLockExclusive(&gOverrideLock);
    if (changed && gWakeEvent) {
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
