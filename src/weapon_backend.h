#pragma once

#include <cstdint>

struct AudioVector {
    float x{};
    float y{};
    float z{};
};

struct AudioCameraTransform {
    AudioVector origin{};
    AudioVector xAxis{};
    AudioVector yAxis{};
    AudioVector zAxis{};
};

struct GunAudioJob {
    std::int16_t drySoundId{-1};
    std::int16_t subSoundId{-1};
    std::int16_t mainLeftSoundId{-1};
    std::int16_t mainRightSoundId{-1};
    std::int16_t tailSoundId{-1};
    float volumeOffsetDb{};
    AudioVector relativePosition{};
    AudioVector worldPosition{};
    float defaultVolumeDb{};
    float baseRollOffFactor{1.0f};
    float baseSpeed{1.0f};
    float mainSpeed{1.0f};
    float tailLeftSpeed{1.0f};
    float tailRightSpeed{1.1892101f};
    float effectsGainDb{};
    bool isAircraftWeapon{};
};

bool WeaponBackendStart(void* module);
void WeaponBackendStop();
bool WeaponBackendEnqueue(const GunAudioJob& job);
bool WeaponBackendShouldReplaceOriginal();
void WeaponBackendSetEnabled(bool enabled);
bool WeaponBackendIsEnabled();
bool WeaponBackendSetSampleOverride(
    std::int16_t soundId,
    const char* path
);
bool WeaponBackendClearSampleOverride(std::int16_t soundId);
void WeaponBackendUpdateCameraTransform(
    const AudioCameraTransform& transform
);
void WeaponBackendUpdateEnvironment(bool canSeeOutside);
