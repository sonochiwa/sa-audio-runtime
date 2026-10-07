#pragma once

#include <cstddef>
#include <cstdint>

// Debug autotest: the log and the game calls the scene makes.
namespace autotest {

struct Vector {
    float x;
    float y;
    float z;
};

// Buttons the scene holds on the player's pad until it changes them.
struct PadState {
    std::int16_t stickX{};
    bool accelerate{};
    bool brake{};
    bool handbrake{};
    bool horn{};
};

bool OpenLog(const char* path);
void Log(const char* format, ...);

bool VerifySites();
std::uint32_t GameTimeMs();
std::int32_t GameState();
void RequestQuit();
void SetUserPause(bool paused);

std::uint8_t* Player();
Vector Position(const void* entity);
Vector Forward(const void* entity);
Vector Ahead(const void* entity, float distance, float side = 0.0f);
float Distance(const Vector& a, const Vector& b);
void Teleport(void* entity, const Vector& position);
Vector Ground(float x, float y, float above);
void ShowPlayer();
void KeepPlayerAlive();
void SetClockAndWeather(std::uint8_t hour, std::int16_t weather);
void SetWantedLevel(std::int32_t level);

std::uint8_t* SpawnVehicle(std::int32_t model);
void PutPlayerInVehicle(std::uint8_t* vehicle);
void DeleteVehicle(std::uint8_t* vehicle);
std::uint8_t* SpawnPed(const Vector& position);
void PedSay(std::uint8_t* ped, std::uint16_t context);
void PedAudioEvent(std::uint8_t* ped, std::int32_t event);

void WeaponFire(std::int32_t weaponType, std::int32_t event);
void Explosion(std::int32_t type, const Vector& position);
void StartFire(const Vector& position, std::uint32_t burnMs);
void FrontendEvent(std::int32_t event);
void BulletHit(std::int32_t surface, const Vector& position, float angle);
void ResetAudioEngine();
void PreloadMissionAudio(std::uint8_t slot, std::int32_t id);
bool MissionAudioLoaded(std::uint8_t slot);
void PlayMissionAudio(std::uint8_t slot);

// The game's own listener volume for a copy of a CAESound at position.
float GameListenerVolume(const std::uint8_t* sound, const Vector* position);

void SetPad(const PadState& state);
void ApplyPad();

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

} // namespace autotest
