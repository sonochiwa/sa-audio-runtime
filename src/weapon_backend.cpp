#include "weapon_backend.h"

#include "backend/backend.h"

using namespace backend;

bool WeaponBackendStart(void* module) {
    gModule = static_cast<HMODULE>(module);
    gReady.store(false, std::memory_order_release);
    gDeviceRecoveryRequested.store(false, std::memory_order_release);
    gWrite.store(0, std::memory_order_release);
    gRead.store(0, std::memory_order_release);
    gResetEpoch.store(0, std::memory_order_release);
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

void WeaponBackendReset() {
    gResetEpoch.fetch_add(1, std::memory_order_acq_rel);
    if (gWakeEvent) {
        SetEvent(gWakeEvent);
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
