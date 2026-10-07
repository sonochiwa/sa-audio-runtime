#include "backend/backend.h"

namespace backend {

std::atomic<std::uint32_t> gVehicleSourceTimeouts{};
std::atomic<std::uint32_t> gMinigunStops{};
std::atomic<std::uint32_t> gStartFailures{};
std::atomic<std::int32_t> gLastFailedStart{-1};

namespace {

std::atomic<bool> gSnapshotsEnabled{};
SRWLOCK gSnapshotLock = SRWLOCK_INIT;
BackendDebugSnapshot gSnapshot;

} // namespace

void PublishDebugSnapshot(
    const std::vector<Voice>& voices,
    std::size_t vehicleSources,
    std::size_t minigunSources,
    std::size_t virtualSources
) {
    if (!gSnapshotsEnabled.load(std::memory_order_acquire)) {
        return;
    }
    AcquireSRWLockExclusive(&gSnapshotLock);
    ++gSnapshot.pass;
    gSnapshot.vehicleSources = static_cast<std::uint32_t>(vehicleSources);
    gSnapshot.minigunSources = static_cast<std::uint32_t>(minigunSources);
    gSnapshot.virtualSources = static_cast<std::uint32_t>(virtualSources);
    gSnapshot.vehicleSourceTimeouts =
        gVehicleSourceTimeouts.load(std::memory_order_relaxed);
    gSnapshot.minigunStops = gMinigunStops.load(std::memory_order_relaxed);
    gSnapshot.startFailures = gStartFailures.load(std::memory_order_relaxed);
    const auto failed = gLastFailedStart.load(std::memory_order_relaxed);
    gSnapshot.lastFailedBank = static_cast<std::int16_t>(failed >> 16);
    gSnapshot.lastFailedSound = static_cast<std::int16_t>(failed & 0xFFFF);
    gSnapshot.voices.clear();
    for (const auto& voice : voices) {
        if (!voice.buffer) {
            continue;
        }
        BackendDebugVoice entry{};
        entry.sourceKey = voice.vehicleSourceKey != 0
            ? voice.vehicleSourceKey
            : voice.minigunSourceKey;
        entry.soundId = voice.soundId;
        entry.bankId = voice.vehicleBankId;
        entry.mixVolumeDb = voice.mixVolumeDb;
        entry.playbackSpeed = voice.playbackSpeed;
        entry.dopplerScale = voice.dopplerScale;
        entry.sourceVolumeDb = voice.sourceVolumeDb;
        entry.worldPosition = voice.worldPosition;
        entry.generation = voice.vehicleGeneration;
        entry.looping = voice.looping;
        entry.pending = voice.pendingStart;
        entry.suspended = voice.suspended;
        entry.frontEnd = voice.isFrontEnd;
        entry.stopFading = voice.vehicleStopFading || voice.minigunTailFading;
        gSnapshot.voices.push_back(entry);
    }
    ReleaseSRWLockExclusive(&gSnapshotLock);
}

} // namespace backend

using namespace backend;

void WeaponBackendEnableDebugSnapshots() {
    gSnapshotsEnabled.store(true, std::memory_order_release);
}

bool WeaponBackendReadDebugSnapshot(BackendDebugSnapshot& snapshot) {
    if (!gSnapshotsEnabled.load(std::memory_order_acquire)) {
        return false;
    }
    AcquireSRWLockShared(&gSnapshotLock);
    snapshot = gSnapshot;
    ReleaseSRWLockShared(&gSnapshotLock);
    return snapshot.pass != 0;
}
