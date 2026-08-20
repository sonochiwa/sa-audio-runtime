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

enum class MinigunAudioMode : std::uint8_t {
    None,
    Fire,
    Spin
};

enum class AudioJobType : std::uint8_t {
    Gunshot,
    BulletHit,
    VehicleUpdate,
    VehicleStop,
    VehicleOneShot,
    DialogueStart,
    DialogueUpdate,
    DialogueStop,
    GenericOneShot,
    StatefulStart,
    StatefulUpdate,
    StatefulStop
};

enum class RuntimeSoundBank : std::uint8_t {
    Weapons,
    BulletHits,
    Count
};

struct AudioJob {
    AudioJobType type{AudioJobType::Gunshot};
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
    MinigunAudioMode minigunMode{MinigunAudioMode::None};
    std::uintptr_t sourceKey{};
    float minigunStopVolumeDb{};
    std::uint32_t sourceGeneration{};
    std::int16_t bankId{-1};
    std::int16_t playTime{};
    float dopplerScale{1.0f};
    bool startPercentage{};
    bool keepAliveWhenSilent{};
    bool isFrontEnd{};
    bool isUnpausable{};
    bool reportPlayTime{};
};

struct AudioCompletion {
    std::uintptr_t sourceKey{};
    std::uint32_t sourceGeneration{};
    bool finished{true};
    std::int16_t lengthMs{-1};
};

bool WeaponBackendStart(void* module);
void WeaponBackendStop();
void WeaponBackendReset();
bool WeaponBackendEnqueue(const AudioJob& job);
bool WeaponBackendShouldReplaceOriginal();
void WeaponBackendSetEnabled(bool enabled);
bool WeaponBackendIsEnabled();
bool VehicleBackendShouldReplaceOriginal();
void VehicleBackendSetEnabled(bool enabled);
bool VehicleBackendEnqueue(const AudioJob& job);
bool DialogueBackendShouldReplaceOriginal();
void DialogueBackendSetEnabled(bool enabled);
bool DialogueBackendEnqueue(const AudioJob& job);
bool DialogueBackendPollCompletion(AudioCompletion& completion);
bool WeaponBackendSetSampleOverride(
    RuntimeSoundBank bank,
    std::int16_t soundId,
    const char* path
);
bool WeaponBackendClearSampleOverride(
    RuntimeSoundBank bank,
    std::int16_t soundId
);
void WeaponBackendSetArchiveOverride(
    const char* archivePath,
    const char* lookupPath
);
bool WeaponBackendSetDynamicSampleOverride(
    std::int16_t bankId,
    std::int16_t soundId,
    const char* path,
    bool installed
);
bool WeaponBackendSetPackOverride(
    std::int32_t packId,
    const char* path,
    bool installed
);
void WeaponBackendUpdateCameraTransform(
    const AudioCameraTransform& transform
);
void WeaponBackendUpdateEnvironment(bool canSeeOutside);
