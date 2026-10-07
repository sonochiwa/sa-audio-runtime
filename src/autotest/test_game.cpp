#include "autotest/test_game.h"

#include "autotest/test_addresses.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace autotest {
namespace {

using PlayerPedFn = std::uint8_t*(__cdecl*)(std::int32_t);
using WantedFn = void*(__cdecl*)(std::int32_t);
using SetWantedFn = void(__thiscall*)(void*, std::int32_t);
using VehicleCheatFn = std::uint8_t*(__cdecl*)(std::int32_t);
using PedInCarFn = void(__cdecl*)(void*, void*, std::int32_t, bool);
using RemoveFn = void(__cdecl*)(void*);
using CleanUpReferenceFn = void(__thiscall*)(void*, void**);
using DeleteFn = void(__thiscall*)(void*, std::uint32_t);
using AddPedFn = std::uint8_t*(__cdecl*)(std::int32_t, std::int32_t, const Vector&, bool);
using TeleportFn = void(__thiscall*)(void*, Vector, bool);
using LoadSceneFn = void(__cdecl*)(const Vector*);
using GroundFn = float(__cdecl*)(float, float);
using FadeFn = void(__thiscall*)(void*, float, std::uint16_t);
using CameraFn = void(__thiscall*)(void*);
using ExplosionFn = void(__cdecl*)(
    void*, void*, std::int32_t, Vector, std::uint32_t, std::uint8_t, float, std::uint8_t);
using StartFireFn = void*(__thiscall*)(
    void*, Vector, float, std::uint8_t, void*, std::uint32_t, std::int8_t, std::uint8_t);
using WeatherFn = void(__cdecl*)(std::int16_t);
using ClockFn = void(__cdecl*)(std::uint8_t, std::uint8_t, std::uint8_t);
using SayFn = std::int16_t(__thiscall*)(void*, std::uint16_t, std::uint32_t, float, bool, bool,
                                        bool);
using PedEventFn = void(__thiscall*)(void*, std::int32_t, float, float, void*, std::int32_t,
                                     std::int32_t, std::uint32_t);
using WeaponFireFn = void(__thiscall*)(void*, std::int32_t, void*, std::int32_t);
using FrontendFn = void(__thiscall*)(void*, std::int32_t, float, float);
using BulletHitFn = void(__thiscall*)(void*, void*, std::int32_t, const Vector&, float);
using EngineFn = void(__thiscall*)(void*);
using PreloadFn = void(__thiscall*)(void*, std::uint8_t, std::int32_t);
using StatusFn = std::int8_t(__thiscall*)(void*, std::uint8_t);
using PlayFn = void(__thiscall*)(void*, std::uint8_t);
using CalculateVolumeFn = void(__thiscall*)(void*);
using HeadroomFn = float(__thiscall*)(void*, std::int16_t, std::int16_t);

constexpr std::size_t kSoundSize = 0x74;
constexpr std::size_t kSoundBankSlot = 0x0;
constexpr std::size_t kSoundId = 0x2;
constexpr std::size_t kSoundPosition = 0x24;
constexpr float kPlayerHealth = 1000.0f;
constexpr std::int16_t kButtonDown = 255;
constexpr std::int32_t kCivilianMale = 4;
constexpr std::int32_t kMaleModel = 7;

std::FILE* gLog{};
SRWLOCK gLogLock = SRWLOCK_INIT;
PadState gPad{};

template <typename T>
T& At(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

template <typename T>
T Function(const CodeSite& site) {
    return reinterpret_cast<T>(site.address);
}

void* AudioEngine() {
    return reinterpret_cast<void*>(kAudioEngine);
}

bool Matches(const CodeSite& site) {
    const auto* code = reinterpret_cast<const std::uint8_t*>(site.address);
    if (site.detourAllowed && code[0] == 0xE9) {
        return true;
    }
    const char* text = site.bytes;
    for (std::size_t index = 0; *text; ++index) {
        while (*text == ' ') {
            ++text;
        }
        if (!*text) {
            break;
        }
        if (text[0] != '?') {
            char* end{};
            const auto value = std::strtoul(text, &end, 16);
            if (code[index] != value) {
                return false;
            }
        }
        text += 2;
    }
    return true;
}

} // namespace

bool OpenLog(const char* path) {
    gLog = std::fopen(path, "w");
    return gLog != nullptr;
}

void Log(const char* format, ...) {
    if (!gLog) {
        return;
    }
    char line[1024]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    AcquireSRWLockExclusive(&gLogLock);
    std::fprintf(gLog, "%8llu %s\n", GetTickCount64(), line);
    std::fflush(gLog);
    ReleaseSRWLockExclusive(&gLogLock);
}

bool VerifySites() {
    bool allMatch = true;
    for (const auto* site : kSites) {
        if (!Matches(*site)) {
            Log("autotest: unexpected code at 0x%06X (%s)",
                static_cast<unsigned>(site->address), site->name);
            allMatch = false;
        }
    }
    return allMatch;
}

std::uint32_t GameTimeMs() {
    return At<std::uint32_t>(kGameTime);
}

std::int32_t GameState() {
    return At<std::int32_t>(kGameState);
}

void RequestQuit() {
    At<std::int32_t>(kRsGlobalQuit) = 1;
}

void SetUserPause(bool paused) {
    At<std::uint8_t>(kUserPause) = paused ? 1 : 0;
}

std::uint8_t* Player() {
    return Function<PlayerPedFn>(kFindPlayerPed)(-1);
}

Vector Position(const void* entity) {
    const auto* matrix = *reinterpret_cast<const std::uint8_t* const*>(
        static_cast<const std::uint8_t*>(entity) + kPlaceableMatrix);
    return *reinterpret_cast<const Vector*>(matrix + kMatrixPosition);
}

Vector Forward(const void* entity) {
    const auto* matrix = *reinterpret_cast<const std::uint8_t* const*>(
        static_cast<const std::uint8_t*>(entity) + kPlaceableMatrix);
    return *reinterpret_cast<const Vector*>(matrix + kMatrixForward);
}

Vector Ahead(const void* entity, float distance, float side) {
    const auto position = Position(entity);
    const auto forward = Forward(entity);
    return {position.x + forward.x * distance + forward.y * side,
            position.y + forward.y * distance - forward.x * side, position.z};
}

float Distance(const Vector& a, const Vector& b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

void Teleport(void* entity, const Vector& position) {
    const auto* vtable = Field<const std::uintptr_t*>(entity, 0);
    reinterpret_cast<TeleportFn>(vtable[kEntityTeleportSlot])(entity, position, false);
}

Vector Ground(float x, float y, float above) {
    const Vector around{x, y, 0.0f};
    Function<LoadSceneFn>(kLoadScene)(&around);
    return {x, y, Function<GroundFn>(kFindGroundZ)(x, y) + above};
}

void ShowPlayer() {
    auto* camera = reinterpret_cast<void*>(kTheCamera);
    Function<CameraFn>(kCameraBehind)(camera);
    Function<FadeFn>(kCameraFade)(camera, 0.0f, kFadeIn);
}

void KeepPlayerAlive() {
    if (auto* player = Player()) {
        Field<float>(player, kPedHealth) = kPlayerHealth;
    }
}

void SetClockAndWeather(std::uint8_t hour, std::int16_t weather) {
    Function<ClockFn>(kSetGameClock)(hour, 0, 0);
    Function<WeatherFn>(kForceWeatherNow)(weather);
}

void SetWantedLevel(std::int32_t level) {
    if (auto* wanted = Function<WantedFn>(kFindPlayerWanted)(-1)) {
        Function<SetWantedFn>(kSetWantedLevel)(wanted, level);
    }
}

std::uint8_t* SpawnVehicle(std::int32_t model) {
    auto* vehicle = Function<VehicleCheatFn>(kVehicleCheat)(model);
    if (vehicle) {
        Field<std::uint32_t>(vehicle, kVehicleCreatedBy) = kMissionVehicle;
    }
    Log("scene: vehicle %d %s", model, vehicle ? "spawned" : "NOT SPAWNED");
    return vehicle;
}

void PutPlayerInVehicle(std::uint8_t* vehicle) {
    auto* player = Player();
    if (vehicle && player && Field<void*>(vehicle, kVehicleDriver) != player) {
        Function<PedInCarFn>(kSetPedInCarDirect)(player, vehicle, 0, true);
    }
}

// The driver is unregistered first: the vehicle destructor would otherwise
// flag the player for deletion, and the player would later clear a
// reference inside the freed vehicle.
void DeleteVehicle(std::uint8_t* vehicle) {
    if (!vehicle) {
        return;
    }
    auto*& driver = Field<void*>(vehicle, kVehicleDriver);
    if (driver) {
        Function<CleanUpReferenceFn>(kCleanUpOldReference)(driver, &driver);
        driver = nullptr;
    }
    auto& status = Field<std::uint8_t>(vehicle, kEntityStatusByte);
    status = static_cast<std::uint8_t>((status & kEntityStatusKeep) | kStatusAbandoned);
    Function<RemoveFn>(kWorldRemove)(vehicle);
    const auto* vtable = Field<const std::uintptr_t*>(vehicle, 0);
    reinterpret_cast<DeleteFn>(vtable[kEntityDeleteSlot])(vehicle, 1);
}

std::uint8_t* SpawnPed(const Vector& position) {
    auto* ped = Function<AddPedFn>(kAddPed)(kCivilianMale, kMaleModel, position, false);
    Log("scene: ped %s", ped ? "spawned" : "NOT SPAWNED");
    return ped;
}

void PedSay(std::uint8_t* ped, std::uint16_t context) {
    const auto result = Function<SayFn>(kPedSay)(ped, context, 0, 1.0f, true, true, false);
    Log("scene: ped says context %u -> %d", static_cast<unsigned>(context), result);
}

void PedAudioEvent(std::uint8_t* ped, std::int32_t event) {
    Function<PedEventFn>(kPedAudioAddEvent)(ped + kPedAudio, event, 0.0f, 1.0f, ped, 0, 0, 0);
}

void WeaponFire(std::int32_t weaponType, std::int32_t event) {
    if (auto* player = Player()) {
        Function<WeaponFireFn>(kWeaponFire)(player + kPedWeaponAudio, weaponType, player, event);
    }
}

void Explosion(std::int32_t type, const Vector& position) {
    Function<ExplosionFn>(kAddExplosion)(nullptr, nullptr, type, position, 0, 1, 0.0f, 1);
}

void StartFire(const Vector& position, std::uint32_t burnMs) {
    Function<StartFireFn>(kStartFire)(reinterpret_cast<void*>(kFireManager), position, 1.0f, 1,
                                      nullptr, burnMs, 0, 1);
}

void FrontendEvent(std::int32_t event) {
    Function<FrontendFn>(kReportFrontend)(AudioEngine(), event, 0.0f, 1.0f);
}

void BulletHit(std::int32_t surface, const Vector& position, float angle) {
    Function<BulletHitFn>(kReportBulletHit)(AudioEngine(), nullptr, surface, position, angle);
}

void ResetAudioEngine() {
    Function<EngineFn>(kAudioEngineReset)(AudioEngine());
}

void PreloadMissionAudio(std::uint8_t slot, std::int32_t id) {
    Function<PreloadFn>(kPreloadMissionAudio)(AudioEngine(), slot, id);
}

bool MissionAudioLoaded(std::uint8_t slot) {
    return Function<StatusFn>(kMissionAudioStatus)(AudioEngine(), slot) == 1;
}

void PlayMissionAudio(std::uint8_t slot) {
    Function<PlayFn>(kPlayMissionAudio)(AudioEngine(), slot);
}

float GameListenerVolume(const std::uint8_t* sound, const Vector* position) {
    std::array<std::uint8_t, kSoundSize> copy{};
    std::memcpy(copy.data(), sound, copy.size());
    if (position) {
        std::memcpy(copy.data() + kSoundPosition, position, sizeof(*position));
    }
    std::int16_t soundId{};
    std::int16_t bankSlot{};
    std::memcpy(&soundId, sound + kSoundId, sizeof(soundId));
    std::memcpy(&bankSlot, sound + kSoundBankSlot, sizeof(bankSlot));
    const float headroom = Function<HeadroomFn>(kGetSoundHeadroom)(
        reinterpret_cast<void*>(kAudioHardware), soundId, bankSlot);
    std::memcpy(copy.data() + kSoundHeadroom, &headroom, sizeof(headroom));
    Function<CalculateVolumeFn>(kCalculateVolume)(copy.data());
    float volume{};
    std::memcpy(&volume, copy.data() + kSoundListenerVolume, sizeof(volume));
    return volume;
}

void SetPad(const PadState& state) {
    gPad = state;
}

void ApplyPad() {
    auto* pad = reinterpret_cast<std::uint8_t*>(kPlayerPad);
    const auto Button = [pad](std::size_t offset, bool down) {
        if (down) {
            Field<std::int16_t>(pad, offset) = kButtonDown;
        }
    };
    if (gPad.stickX != 0) {
        Field<std::int16_t>(pad, kPadLeftStickX) = gPad.stickX;
    }
    Button(kPadButtonCross, gPad.accelerate);
    Button(kPadButtonSquare, gPad.brake);
    Button(kPadRightShoulder1, gPad.handbrake);
    Button(kPadShockButtonL, gPad.horn);
}

} // namespace autotest
