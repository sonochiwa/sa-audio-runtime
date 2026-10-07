#include "autotest/probe.h"

#include "autotest/test_addresses.h"
#include "autotest/test_game.h"
#include "modules/modules.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>

namespace autotest {
namespace {

// Volume differences below this are the frame of latency between the game
// moving a sound and the worker applying it.
constexpr float kVolumeToleranceDb = 1.5f;
constexpr std::uint32_t kLoggedMismatchesPerStep = 12;
constexpr std::size_t kSoundEvent = 0x0C;
constexpr std::size_t kAudioEntityOwner = 0x04;

using RequestKey = std::tuple<std::int16_t, std::int32_t, std::int16_t, std::uint16_t, char>;

struct StepStats {
    std::string name;
    std::map<RequestKey, std::uint32_t> requests;
    std::uint32_t volumeCompared{};
    std::uint32_t volumeMismatches{};
    float worstVolumeDiffDb{};
    std::uint32_t orphanLoops{};
    std::uint32_t worstVoices{};
};

bool gRuntimePass{};
bool gStepOpen{};
StepStats gStep;

template <typename T>
T Read(const void* sound, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(sound) + offset, sizeof(value));
    return value;
}

bool IsGameSlot(const void* sound) {
    const auto address = reinterpret_cast<std::uintptr_t>(sound);
    return address >= kSoundManager && address < kSoundManager + kSoundManagerSize;
}

char ProxyKind(const void* sound) {
    for (const auto& entry : runtime::gVehicleSoundProxies) {
        if (entry.second.sound.data() == sound) {
            return 'V';
        }
    }
    for (const auto& entry : runtime::gDialogueSoundProxies) {
        if (entry.second.sound.data() == sound) {
            return 'D';
        }
    }
    for (const auto& entry : runtime::gStatefulSoundProxies) {
        if (entry.second.sound.data() == sound) {
            return 'S';
        }
    }
    return 0;
}

char Route(const void* result) {
    if (!result) {
        return 'N';
    }
    if (IsGameSlot(result)) {
        return 'G';
    }
    const char kind = ProxyKind(result);
    return kind ? kind : '?';
}

void ObserveRequest(const void* request, const void* result) {
    if (!gStepOpen) {
        return;
    }
    const RequestKey key{
        Read<std::int16_t>(request, runtime::kAeSoundBankSlotOffset),
        Read<std::int32_t>(request, kSoundEvent),
        Read<std::int16_t>(request, runtime::kAeSoundIdOffset),
        Read<std::uint16_t>(request, runtime::kAeSoundFlagsOffset),
        Route(result)};
    ++gStep.requests[key];
}

const BackendDebugVoice* FindVoice(const BackendDebugSnapshot& snapshot, const void* sound) {
    const auto key = reinterpret_cast<std::uintptr_t>(sound);
    for (const auto& voice : snapshot.voices) {
        if (voice.sourceKey == key && !voice.pending && !voice.suspended &&
            !voice.stopFading) {
            return &voice;
        }
    }
    return nullptr;
}

void CompareVolume(const char* kind, const BackendDebugSnapshot& snapshot,
                   const std::uint8_t* sound, const Vector* position) {
    const auto* voice = FindVoice(snapshot, sound);
    if (!voice) {
        return;
    }
    const float expected = GameListenerVolume(sound, position);
    const float difference = voice->mixVolumeDb - expected;
    ++gStep.volumeCompared;
    gStep.worstVolumeDiffDb = std::max(gStep.worstVolumeDiffDb, std::fabs(difference));
    if (std::fabs(difference) <= kVolumeToleranceDb) {
        return;
    }
    if (gStep.volumeMismatches++ < kLoggedMismatchesPerStep) {
        const auto key = reinterpret_cast<std::uintptr_t>(sound);
        const auto voicesWithKey = std::count_if(
            snapshot.voices.begin(), snapshot.voices.end(),
            [key](const BackendDebugVoice& entry) { return entry.sourceKey == key; });
        const auto gamePosition =
            position ? *position : Read<Vector>(sound, runtime::kAeSoundPositionOffset);
        Log("volume step=%s kind=%s slot=%d sound=%d event=%d frontend=%d worker=%.1f "
            "game=%.1f diff=%.1f source=%.1f/%.1f pos=%.0f,%.0f,%.0f/%.0f,%.0f,%.0f "
            "voices=%d",
            gStep.name.c_str(), kind, Read<std::int16_t>(sound, runtime::kAeSoundBankSlotOffset),
            Read<std::int16_t>(sound, runtime::kAeSoundIdOffset),
            Read<std::int32_t>(sound, kSoundEvent), voice->frontEnd ? 1 : 0,
            voice->mixVolumeDb, expected, difference, voice->sourceVolumeDb,
            Read<float>(sound, runtime::kAeSoundVolumeOffset), voice->worldPosition.x,
            voice->worldPosition.y, voice->worldPosition.z, gamePosition.x, gamePosition.y,
            gamePosition.z, static_cast<int>(voicesWithKey));
    }
}

void CompareProxyVolumes(const BackendDebugSnapshot& snapshot) {
    for (const auto& entry : runtime::gStatefulSoundProxies) {
        if (entry.second.active && entry.second.started) {
            CompareVolume("stateful", snapshot, entry.second.sound.data(), nullptr);
        }
    }
    for (const auto& entry : runtime::gDialogueSoundProxies) {
        if (entry.second.active) {
            CompareVolume("dialogue", snapshot, entry.second.sound.data(), nullptr);
        }
    }
    for (const auto& entry : runtime::gVehicleSoundProxies) {
        const auto& proxy = entry.second;
        auto* vehicle = proxy.active && proxy.owner
            ? Read<void*>(proxy.owner, kAudioEntityOwner)
            : nullptr;
        if (vehicle) {
            const auto position = Position(vehicle);
            CompareVolume("vehicle", snapshot, proxy.sound.data(), &position);
        }
    }
}

bool IsLiveProxy(std::uintptr_t key) {
    return ProxyKind(reinterpret_cast<const void*>(key)) != 0;
}

std::uint32_t CountOrphanLoops(const BackendDebugSnapshot& snapshot) {
    auto* player = Player();
    const auto weaponEntity =
        player ? reinterpret_cast<std::uintptr_t>(player + kPedWeaponAudio) : 0;
    std::uint32_t orphans{};
    for (const auto& voice : snapshot.voices) {
        if (voice.looping && !voice.stopFading && voice.sourceKey != weaponEntity &&
            !IsLiveProxy(voice.sourceKey)) {
            ++orphans;
        }
    }
    return orphans;
}

BackendDebugSnapshot ReadSnapshot() {
    BackendDebugSnapshot snapshot{};
    WeaponBackendReadDebugSnapshot(snapshot);
    return snapshot;
}

} // namespace

void InstallProbe(bool runtimePass) {
    gRuntimePass = runtimePass;
    WeaponBackendEnableDebugSnapshots();
    runtime::gRequestObserver = &ObserveRequest;
}

void BeginStep(const char* name) {
    gStep = {};
    gStep.name = name;
    gStepOpen = true;
    Log("step %s begin", name);
}

void EndStep() {
    if (!gStepOpen) {
        return;
    }
    gStepOpen = false;
    std::uint32_t total{};
    std::uint32_t game{};
    std::uint32_t none{};
    for (const auto& [key, count] : gStep.requests) {
        const auto& [slot, event, sound, flags, route] = key;
        Log("req step=%s slot=%d event=%d sound=%d flags=%04X route=%c n=%u",
            gStep.name.c_str(), slot, event, sound, flags, route, count);
        total += count;
        game += route == 'G' ? count : 0;
        none += route == 'N' ? count : 0;
    }
    Log("step %s end requests=%u game=%u null=%u proxy=%u volcmp=%u volbad=%u volmax=%.1f "
        "orphanloops=%u maxvoices=%u",
        gStep.name.c_str(), total, game, none, total - game - none, gStep.volumeCompared,
        gStep.volumeMismatches, gStep.worstVolumeDiffDb, gStep.orphanLoops, gStep.worstVoices);
}

void Sample() {
    const auto snapshot = ReadSnapshot();
    std::uint32_t loops{};
    std::uint32_t pending{};
    std::uint32_t suspended{};
    for (const auto& voice : snapshot.voices) {
        loops += voice.looping ? 1 : 0;
        pending += voice.pending ? 1 : 0;
        suspended += voice.suspended ? 1 : 0;
    }
    std::uint32_t activeVehicleProxies{};
    for (const auto& entry : runtime::gVehicleSoundProxies) {
        activeVehicleProxies += entry.second.active ? 1 : 0;
    }
    const auto orphans = gRuntimePass ? CountOrphanLoops(snapshot) : 0;
    Log("snap step=%s voices=%u loops=%u pending=%u suspended=%u vsrc=%u msrc=%u virt=%u "
        "vproxy=%u dproxy=%u sproxy=%u vtimeouts=%u mstops=%u orphanloops=%u startfail=%u "
        "lastfail=%d:%d",
        gStep.name.c_str(), static_cast<unsigned>(snapshot.voices.size()), loops, pending,
        suspended, snapshot.vehicleSources, snapshot.minigunSources, snapshot.virtualSources,
        activeVehicleProxies, static_cast<unsigned>(runtime::gDialogueSoundProxies.size()),
        static_cast<unsigned>(runtime::gStatefulSoundProxies.size()),
        snapshot.vehicleSourceTimeouts, snapshot.minigunStops, orphans, snapshot.startFailures,
        snapshot.lastFailedBank, snapshot.lastFailedSound);
    gStep.orphanLoops = std::max(gStep.orphanLoops, orphans);
    gStep.worstVoices =
        std::max(gStep.worstVoices, static_cast<std::uint32_t>(snapshot.voices.size()));
    if (gRuntimePass) {
        CompareProxyVolumes(snapshot);
    }
}

std::uint32_t StepRequests(std::int32_t event) {
    std::uint32_t count{};
    for (const auto& [key, requests] : gStep.requests) {
        count += std::get<1>(key) == event ? requests : 0;
    }
    return count;
}

std::uint32_t VehicleSourceTimeouts() {
    return ReadSnapshot().vehicleSourceTimeouts;
}

std::uint32_t MinigunStops() {
    return ReadSnapshot().minigunStops;
}

std::uint32_t CountDanglingVehicleSounds(const char* when) {
    const auto* pool = *reinterpret_cast<std::uint8_t* const*>(kVehiclePool);
    if (!pool) {
        return 0;
    }
    auto* objects = *reinterpret_cast<std::uint8_t* const*>(pool);
    const auto* byteMap = *reinterpret_cast<const std::uint8_t* const*>(pool + 4);
    const auto size = *reinterpret_cast<const std::int32_t*>(pool + 8);
    std::uint32_t dangling{};
    for (std::int32_t index = 0; index < size; ++index) {
        if (byteMap[index] & 0x80) {
            continue;
        }
        auto* audio = objects + index * kVehiclePoolElementSize + kVehicleAudio;
        const auto Check = [&](void* sound, const char* slot) {
            if (!sound || IsGameSlot(sound) || ProxyKind(sound) != 0) {
                return;
            }
            ++dangling;
            Log("dangling %s vehicle=%d slot=%s sound=%p", when, index, slot, sound);
        };
        for (std::int32_t type = 0;
             type < static_cast<std::int32_t>(runtime::kVehicleEngineSoundCount); ++type) {
            Check(*runtime::GetVehicleEngineSoundSlot(audio, type), "engine");
        }
        for (const auto& descriptor : runtime::kVehiclePersistentSounds) {
            Check(*runtime::GetVehicleSoundPointerSlot(audio, descriptor.pointerOffset),
                  "persistent");
        }
    }
    Log("check dangling-%s: %s (%u pointers)", when, dangling ? "FAIL" : "PASS", dangling);
    return dangling;
}

} // namespace autotest
