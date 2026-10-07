#include "autotest/scene.h"

#include "autotest/probe.h"
#include "autotest/test_addresses.h"
#include "autotest/test_game.h"
#include "modules/modules.h"

#include <windows.h>

#include <cstdint>
#include <iterator>

namespace autotest {
namespace {

using StepFrame = void (*)(std::uint32_t elapsed);

struct Step {
    const char* name;
    std::uint32_t durationMs;
    bool runtimeOnly;
    StepFrame frame;
};

constexpr std::uint32_t kSampleMs = 500;
// A flat, open stretch of the Los Santos airport.
constexpr float kSceneX = 1700.0f;
constexpr float kSceneY = -2450.0f;
constexpr std::int16_t kWeatherSunny = 0;
constexpr std::int16_t kStickLeft = -128;
constexpr std::int32_t kFireEvent = 145;
constexpr std::int32_t kMinigunNoAmmoEvent = 151;
constexpr std::int32_t kPedCrunchEvent = 119;
constexpr std::int32_t kPedKnockDownEvent = 121;
constexpr std::int32_t kMissileLockEvent = 101;
constexpr std::int32_t kPickupWeaponEvent = 6;
constexpr std::int32_t kMinigun = 38;
constexpr std::int32_t kWeapons[] = {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34};
constexpr std::uint32_t kWeaponSlotMs = 1600;
constexpr std::int32_t kSurfaceCount = 179;
constexpr std::uint16_t kContextChat = 45;
constexpr std::uint16_t kContextDodge = 74;
// AE_SCRIPT_SLOT_FIRST: the first sound of the first SCRIPT bank.
constexpr std::int32_t kMissionAudioId = 2000;
constexpr std::int32_t kCarModel = 560;
constexpr std::int32_t kPoliceModel = 596;
constexpr std::int32_t kBikeModel = 522;
constexpr std::int32_t kBmxModel = 481;
constexpr std::int32_t kHeliModel = 487;
constexpr std::int32_t kExplosionGrenade = 0;
constexpr std::int32_t kExplosionRocket = 2;
constexpr std::int32_t kExplosionCar = 4;
constexpr std::int32_t kExplosionSmall = 11;

bool gRuntimePass{};
std::size_t gStepIndex{};
ULONGLONG gStepStart{};
ULONGLONG gLastSample{};
ULONGLONG gLastAction{};
std::int64_t gPrevious{-1};
std::uint32_t gElapsed{};
std::uint32_t gFrames{};
std::uint32_t gMark{};
bool gMissionPlayed{};
PadState gPad{};
std::uint32_t gSampleFrames{};
std::uint8_t* gVehicle{};
std::uint8_t* gPed{};

// True on the first frame at or after `at` milliseconds into the step.
bool At(std::uint32_t at) {
    return gPrevious < static_cast<std::int64_t>(at) && gElapsed >= at;
}

bool Between(std::uint32_t from, std::uint32_t to) {
    return gElapsed >= from && gElapsed < to;
}

// True at most once per period while the step runs.
bool Every(std::uint32_t periodMs) {
    const auto now = GetTickCount64();
    if (now - gLastAction < periodMs) {
        return false;
    }
    gLastAction = now;
    return true;
}

void SwitchVehicle(std::int32_t model) {
    auto* previous = gVehicle;
    gVehicle = SpawnVehicle(model);
    if (!gVehicle) {
        gVehicle = previous;
        return;
    }
    PutPlayerInVehicle(gVehicle);
    DeleteVehicle(previous);
}

// The vanilla pass keeps the runtime off, so the steps that switch it play
// there as the baseline of what the game does by itself.
void SetRuntime(bool enabled) {
    if (!gRuntimePass) {
        return;
    }
    AudioConfigOverrideEnabled(enabled);
    runtime::ApplyRuntimeState();
    Log("scene: runtime %s", enabled ? "on" : "off");
}

void LogSkid(const char* when) {
    if (!gVehicle) {
        return;
    }
    auto* audio = gVehicle + kVehicleAudio;
    Log("scene: skid %s: type %d, twin loop in use %d", when,
        Field<std::int16_t>(audio, kVehicleAudioSkidType),
        Field<std::int16_t>(audio, runtime::kVehicleSkidInUseOffset));
}

void Setup(std::uint32_t) {
    if (At(0)) {
        SetClockAndWeather(12, kWeatherSunny);
        SetWantedLevel(0);
        Teleport(Player(), Ground(kSceneX, kSceneY, 1.0f));
        ShowPlayer();
    }
    if (At(1500)) {
        ShowPlayer();
    }
}

void Guns(std::uint32_t elapsed) {
    const auto slot = elapsed / kWeaponSlotMs;
    if (slot < std::size(kWeapons) && elapsed % kWeaponSlotMs < 1000 && Every(150)) {
        WeaponFire(kWeapons[slot], kFireEvent);
    }
}

void Minigun(std::uint32_t) {
    if (Between(0, 2000) && Every(50)) {
        WeaponFire(kMinigun, kFireEvent);
    } else if (Between(2000, 3000) && Every(50)) {
        WeaponFire(kMinigun, kMinigunNoAmmoEvent);
    }
}

// The game's minigun keeps firing through a pause: its stop timer runs on
// game time, which a pause holds.
void MinigunPause(std::uint32_t) {
    if ((Between(0, 1000) || Between(2500, 3500)) && Every(50)) {
        WeaponFire(kMinigun, kFireEvent);
    }
    if (At(1000)) {
        gMark = MinigunStops();
        SetUserPause(true);
    }
    if (At(2500)) {
        SetUserPause(false);
    }
    if (At(3400)) {
        const auto stops = MinigunStops() - gMark;
        Log("check minigun-pause: %s (%u stop sounds while firing through a pause)",
            stops == 0 ? "PASS" : "FAIL", stops);
    }
}

// The same with a short break after the pause: the game's minigun stops once
// 300 ms of game time pass without fire, and 200 ms is less.
void MinigunPauseGap(std::uint32_t) {
    if ((Between(0, 1000) || Between(2700, 3700)) && Every(50)) {
        WeaponFire(kMinigun, kFireEvent);
    }
    if (At(1000)) {
        gMark = MinigunStops();
        SetUserPause(true);
    }
    if (At(2500)) {
        SetUserPause(false);
    }
    if (At(2650)) {
        const auto stops = MinigunStops() - gMark;
        Log("check minigun-pause-gap: %s (%u stop sounds within 300 ms of game time)",
            stops == 0 ? "PASS" : "FAIL", stops);
    }
}

void BulletHits(std::uint32_t elapsed) {
    const auto surface = static_cast<std::int32_t>(elapsed / 60);
    if (surface < kSurfaceCount && At(static_cast<std::uint32_t>(surface) * 60)) {
        const bool left = surface % 2 != 0;
        BulletHit(surface, Ahead(Player(), 6.0f, left ? 2.0f : -2.0f), left ? 30.0f : 120.0f);
    }
}

void Explosions(std::uint32_t) {
    if (At(0)) {
        Explosion(kExplosionGrenade, Ahead(Player(), 20.0f));
    }
    if (At(3000)) {
        Explosion(kExplosionRocket, Ahead(Player(), 30.0f, 10.0f));
    }
    if (At(6000)) {
        Explosion(kExplosionCar, Ahead(Player(), 40.0f, -10.0f));
    }
    if (At(9000)) {
        Explosion(kExplosionSmall, Ahead(Player(), 15.0f));
    }
}

void Fire(std::uint32_t) {
    if (At(0)) {
        StartFire(Ahead(Player(), 12.0f), 7000);
    }
}

// CAEPedAudioEntity plays AE_PED_CRUNCH and AE_PED_KNOCK_DOWN only while no
// sound of that event is playing, so a burst of reports makes one sound each.
void PedSounds(std::uint32_t) {
    if (At(0)) {
        const auto ahead = Ahead(Player(), 4.0f);
        gPed = SpawnPed(Ground(ahead.x, ahead.y, 1.0f));
    }
    if (!gPed) {
        return;
    }
    if (gElapsed >= 500 && gFrames < 10) {
        PedAudioEvent(gPed, kPedCrunchEvent);
        ++gFrames;
    }
    if (At(2500)) {
        Log("check crunch-guard: %u crunch and %u knock-down requests from 10 reports",
            StepRequests(kPedCrunchEvent), StepRequests(kPedKnockDownEvent));
    }
    if (At(3000)) {
        PedSay(gPed, kContextChat);
    }
    if (At(7000)) {
        PedSay(gPed, kContextDodge);
    }
}

// CAEFrontendAudioEntity plays AE_MISSILE_LOCK only while none is playing.
void Frontend(std::uint32_t) {
    if (Between(0, 1500)) {
        FrontendEvent(kMissileLockEvent);
    }
    if (At(1600)) {
        Log("check missile-lock-guard: %u requests from one report per frame for 1.5 s",
            StepRequests(kMissileLockEvent));
    }
    if (At(2000)) {
        FrontendEvent(kPickupWeaponEvent);
    }
}

void MissionAudio(std::uint32_t) {
    if (At(0)) {
        gMissionPlayed = false;
        PreloadMissionAudio(0, kMissionAudioId);
    }
    if (!gMissionPlayed && MissionAudioLoaded(0)) {
        PlayMissionAudio(0);
        gMissionPlayed = true;
        Log("scene: mission audio %d playing", kMissionAudioId);
    }
    if (At(8500) && !gMissionPlayed) {
        Log("scene: mission audio %d NOT LOADED", kMissionAudioId);
    }
}

void Car(std::uint32_t) {
    if (At(0)) {
        SwitchVehicle(kCarModel);
    }
    gPad.accelerate = Between(3000, 7000) || Between(9000, 13000);
    gPad.brake = Between(7000, 9000) || Between(16000, 19000);
    gPad.handbrake = Between(10000, 13000);
    gPad.stickX = Between(10000, 13000) ? kStickLeft : 0;
    gPad.horn = Between(13000, 13150) || Between(13400, 13550) || Between(14000, 16000);
    // The game's vehicle sounds hold through a pause; a source the worker
    // times out fades and restarts.
    if (At(21000)) {
        gMark = VehicleSourceTimeouts();
        SetUserPause(true);
    }
    if (At(22500)) {
        SetUserPause(false);
    }
    if (At(23500)) {
        const auto timeouts = VehicleSourceTimeouts() - gMark;
        Log("check vehicle-pause: %s (%u vehicle sources timed out over a pause)",
            timeouts == 0 ? "PASS" : "FAIL", timeouts);
    }
}

// Switching the runtime off mid-skid and mid-horn hands the sounds back to
// the game, which must start them again.
void Toggle(std::uint32_t) {
    gPad.accelerate = Between(0, 6000);
    gPad.handbrake = Between(2000, 6000);
    gPad.stickX = Between(2000, 6000) ? kStickLeft : 0;
    gPad.horn = Between(7000, 9500);
    if (At(2500) || At(3000) || At(3400)) {
        LogSkid("before the switch");
    }
    if (At(3500) || At(8000)) {
        SetRuntime(false);
    }
    if (At(4300) && gVehicle) {
        auto* audio = gVehicle + kVehicleAudio;
        const auto skidType = Field<std::int16_t>(audio, kVehicleAudioSkidType);
        const auto skidInUse = Field<std::int16_t>(audio, runtime::kVehicleSkidInUseOffset);
        const auto roadNoiseType = Field<std::int16_t>(audio, kVehicleAudioRoadNoiseType);
        Log("check skid-after-toggle: %s (skid type %d, twin loop in use %d, road noise "
            "type %d)",
            skidType != -1 && skidInUse == 0 ? "FAIL" : "PASS", skidType, skidInUse,
            roadNoiseType);
    }
    if (At(8800) && gVehicle) {
        auto* horn = Field<std::uint8_t*>(gVehicle + kVehicleAudio,
                                          runtime::kVehicleHornSoundOffset);
        const auto address = reinterpret_cast<std::uintptr_t>(horn);
        const bool gameSound =
            address >= kSoundManager && address < kSoundManager + kSoundManagerSize;
        Log("check horn-after-toggle: %s (horn sound %p)", gameSound ? "PASS" : "FAIL", horn);
    }
    if (At(6000) || At(9500)) {
        SetRuntime(true);
    }
}

void Police(std::uint32_t) {
    if (At(0)) {
        SwitchVehicle(kPoliceModel);
    }
    gPad.horn = Between(500, 700) || Between(12500, 12700);
    gPad.accelerate = Between(1500, 7000);
    gPad.stickX = Between(1500, 7000) ? static_cast<std::int16_t>(30) : 0;
    if (At(2000)) {
        SetWantedLevel(2);
    }
    // Cleared while still driving: a car standing among police gets the
    // player arrested, and the rest of the scene would run without him.
    if (At(6000)) {
        SetWantedLevel(0);
    }
}

void Drive(std::int32_t model, std::uint32_t until) {
    if (At(0)) {
        SwitchVehicle(model);
    }
    gPad.accelerate = Between(500, until);
}

void Bike(std::uint32_t) {
    Drive(kBikeModel, 4500);
}

void Bmx(std::uint32_t) {
    Drive(kBmxModel, 4500);
}

void Heli(std::uint32_t) {
    Drive(kHeliModel, 6000);
}

void Stress(std::uint32_t elapsed) {
    static ULONGLONG lastExplosion;
    static ULONGLONG lastHit;
    const auto now = GetTickCount64();
    if (At(0)) {
        lastExplosion = 0;
        lastHit = 0;
    }
    if (now - lastExplosion >= 300) {
        lastExplosion = now;
        const auto index = elapsed / 300;
        Explosion(index % 2 ? kExplosionGrenade : kExplosionSmall,
                  Ahead(Player(), 25.0f + static_cast<float>(index % 5) * 8.0f,
                        static_cast<float>(index % 7) * 6.0f - 18.0f));
    }
    if (now - lastHit >= 40) {
        lastHit = now;
        const auto index = static_cast<std::int32_t>(elapsed / 40);
        BulletHit(index % kSurfaceCount, Ahead(Player(), 8.0f, index % 2 ? 3.0f : -3.0f), 45.0f);
    }
    if (Every(60)) {
        WeaponFire(kWeapons[(elapsed / 60) % std::size(kWeapons)], kFireEvent);
    }
}

void Quiet(std::uint32_t) {
}

// CAudioEngine::Reset runs when a new game starts or a save loads from the
// pause menu; the vehicles it leaves behind must hold no freed sound.
void Reset(std::uint32_t) {
    if (At(0)) {
        CountDanglingVehicleSounds("before-reset");
        ResetAudioEngine();
        Log("scene: audio engine reset");
    }
    if (At(300)) {
        CountDanglingVehicleSounds("after-reset");
    }
}

constexpr Step kSteps[] = {
    {"setup", 4000, false, &Setup},
    {"guns", static_cast<std::uint32_t>(std::size(kWeapons)) * kWeaponSlotMs, false, &Guns},
    {"minigun", 6000, false, &Minigun},
    {"minigun-pause", 6000, false, &MinigunPause},
    {"minigun-pause-gap", 6000, false, &MinigunPauseGap},
    {"bullet-hits", kSurfaceCount * 60 + 1500, false, &BulletHits},
    {"explosions", 13000, false, &Explosions},
    {"fire", 9000, false, &Fire},
    {"ped", 12000, false, &PedSounds},
    {"frontend", 3500, false, &Frontend},
    {"mission-audio", 9000, false, &MissionAudio},
    {"car", 25000, false, &Car},
    {"toggle", 11000, false, &Toggle},
    {"police", 14000, false, &Police},
    {"bike", 6000, false, &Bike},
    {"bmx", 6000, false, &Bmx},
    {"heli", 8000, false, &Heli},
    {"stress", 10000, false, &Stress},
    {"quiet", 5000, false, &Quiet},
    {"reset", 1500, false, &Reset},
};

bool OpenStep() {
    while (gStepIndex < std::size(kSteps) && kSteps[gStepIndex].runtimeOnly && !gRuntimePass) {
        Log("step %s skipped in the vanilla pass", kSteps[gStepIndex].name);
        ++gStepIndex;
    }
    if (gStepIndex >= std::size(kSteps)) {
        return false;
    }
    gStepStart = GetTickCount64();
    gPrevious = -1;
    gFrames = 0;
    gLastAction = 0;
    BeginStep(kSteps[gStepIndex].name);
    return true;
}

} // namespace

void StartScene(bool runtimePass) {
    gRuntimePass = runtimePass;
    gStepIndex = 0;
    OpenStep();
}

bool SceneFrame() {
    if (gStepIndex >= std::size(kSteps)) {
        return true;
    }
    KeepPlayerAlive();
    const auto now = GetTickCount64();
    gElapsed = static_cast<std::uint32_t>(now - gStepStart);
    gPad = {};
    kSteps[gStepIndex].frame(gElapsed);
    SetPad(gPad);
    gPrevious = gElapsed;
    ++gSampleFrames;
    if (now - gLastSample >= kSampleMs) {
        const auto fps = gSampleFrames * 1000 / (now - gLastSample);
        auto* player = Player();
        const auto where = player ? Position(player) : Vector{};
        Log("scene: %u fps, player at %.0f %.0f %.0f, %s", static_cast<unsigned>(fps), where.x,
            where.y, where.z,
            player && gVehicle && Field<std::uint8_t*>(player, kPedVehicle) == gVehicle
                ? "in the scene vehicle"
                : "not in the scene vehicle");
        gSampleFrames = 0;
        gLastSample = now;
        Sample();
    }
    if (gElapsed >= kSteps[gStepIndex].durationMs) {
        Sample();
        EndStep();
        ++gStepIndex;
        SetPad({});
        if (!OpenStep()) {
            return true;
        }
    }
    return false;
}

} // namespace autotest
