#include "backend/backend.h"

namespace backend {

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
std::atomic<std::uint32_t> gResetEpoch{};
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

} // namespace backend
