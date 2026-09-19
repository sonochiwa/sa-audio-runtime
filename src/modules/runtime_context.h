#pragma once

#include "modules/prelude.h"

namespace runtime {

constexpr std::uintptr_t kPlayGunSoundsAddress = 0x503CE0;
constexpr std::uintptr_t kPlayMinigunFireSoundsAddress = 0x5047C0;
constexpr std::uintptr_t kPlayBulletHitSoundAddress = 0x4DB7C0;
constexpr std::uintptr_t kVehicleAudioServiceAddress = 0x502280;
constexpr std::uintptr_t kVehicleAudioTerminateAddress = 0x4FB8C0;
constexpr std::uintptr_t kRequestPlayerEngineSoundAddress = 0x4F7A50;
constexpr std::uintptr_t kStartDummyEngineSoundAddress = 0x4F7F20;
constexpr std::uintptr_t kCancelVehicleEngineSoundAddress = 0x4F55C0;
constexpr std::uintptr_t kRequestNewSoundAddress = 0x4EFB10;
constexpr std::uintptr_t kRequestNewSoundHotpatchAddress =
    kRequestNewSoundAddress - 5;
constexpr std::uintptr_t kAreBankSoundsPlayingAddress = 0x4EF520;
constexpr std::uintptr_t kAreEventSoundsPlayingAddress = 0x4EF570;
constexpr std::uintptr_t kAreEventPhysicalSoundsPlayingAddress = 0x4EF5D0;
constexpr std::uintptr_t kCancelEventSoundsAddress = 0x4EFB90;
constexpr std::uintptr_t kCancelEventPhysicalSoundsAddress = 0x4EFBF0;
constexpr std::uintptr_t kCancelBankSlotSoundsAddress = 0x4EFC60;
constexpr std::uintptr_t kCancelOwnedSoundsAddress = 0x4EFCD0;
constexpr std::uintptr_t kPedSpeechTerminateAddress = 0x4E5670;
constexpr std::uintptr_t kPedlessSpeechTerminateAddress = 0x4E6300;
constexpr std::uintptr_t kPoliceScannerDestructorAddress = 0x4E6E00;
constexpr std::uintptr_t kAudioEngineServiceAddress = 0x507750;
constexpr std::uintptr_t kAudioEngineResetAddress = 0x507A90;
constexpr std::uintptr_t kGameSignatureAddress = 0x401000;
constexpr std::uintptr_t kGetPositionRelativeToCameraAddress = 0x4D8340;
constexpr std::uintptr_t kGetDopplerRelativeFrequencyAddress = 0x4D7E40;
constexpr std::uintptr_t kVectorRelativeToCameraAddress = 0x4D80B0;
constexpr std::uintptr_t kCanSeeOutsideAddress = 0x53C4A0;
constexpr std::uintptr_t kAddMessageJumpAddress = 0x69F1E0;
constexpr std::uintptr_t kRandomIntAddress = 0x4D9C10;
constexpr std::uintptr_t kRandomFloatAddress = 0x4D9C50;
constexpr std::uintptr_t kResolveProbabilityAddress = 0x4D9C80;
constexpr std::uintptr_t kEventVolumesPointerAddress = 0xBD00F8;
constexpr std::uintptr_t kAudioHardwareAddress = 0xB5F8B8;
constexpr std::uintptr_t kGameTimeMsAddress = 0xB7CB84;
constexpr std::uintptr_t kSurfaceInfosAddress = 0xB79538;
constexpr std::uintptr_t kIsAudioConcreteAddress = 0x55EA30;
constexpr std::uintptr_t kIsAudioGravelAddress = 0x55EA90;
constexpr std::uintptr_t kIsAudioWoodAddress = 0x55EAB0;
constexpr std::uintptr_t kIsAudioWaterAddress = 0x55EAD0;
constexpr std::uintptr_t kIsAudioMetalAddress = 0x55EAF0;
constexpr std::uintptr_t kIsAudioTileAddress = 0x55EB30;
constexpr std::size_t kEffectMasterScaleOffset = 0x414;
constexpr std::size_t kEffectsFaderScaleOffset = 0x41C;
constexpr std::size_t kNonStreamFaderScaleOffset = 0x420;
constexpr std::int32_t kWeaponFireEvent = 145;
constexpr std::int32_t kWeaponFirePlaneEvent = 149;
constexpr std::int32_t kWeaponFireMinigunAmmoEvent = 150;
constexpr std::int32_t kWeaponFireMinigunNoAmmoEvent = 151;
constexpr std::int32_t kWeaponFireMinigunPlaneEvent = 152;
constexpr std::int32_t kWeaponFireMinigunStopEvent = 153;
constexpr std::int32_t kBulletHitEvent = 117;
constexpr std::int32_t kPedSurface = 62;
constexpr std::int32_t kCollisionSurfaceCount = 195;
constexpr std::int16_t kMinigunDrySoundId = 15;
constexpr std::int16_t kMinigunSubSoundId = 16;
constexpr std::int16_t kMinigunMainLeftSoundId = 11;
constexpr std::int16_t kMinigunMainRightSoundId = 12;
constexpr std::int16_t kMinigunTailSoundId = 13;
constexpr std::uint32_t kGame10UsCompactSignature = 0x53EC8B55;
constexpr std::uint32_t kGame10UsHoodlumSignature = 0x16197BE9;
constexpr std::uint8_t kPlayGunSoundsPrologue[] = {
    0x83, 0xEC, 0x28, 0x53, 0x55, 0x56, 0x57, 0x6A, 0x05, 0x8B, 0xF1
};
constexpr std::uint8_t kPlayMinigunFireSoundsPrologue[] = {
    0x8B, 0x44, 0x24, 0x08, 0x3D, 0x96, 0x00, 0x00,
    0x00, 0x53, 0x56, 0x57, 0x8B, 0xF1
};
constexpr std::uint8_t kPlayBulletHitSoundPrologue[] = {
    0x6A, 0xFF, 0x68, 0x98, 0xBE, 0x83, 0x00,
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50
};
constexpr std::uint8_t kVehicleAudioServicePrologue[] = {
    0x51, 0x56, 0x8B, 0xF1, 0x8A, 0x86, 0xA4, 0x00, 0x00, 0x00
};
constexpr std::uint8_t kVehicleAudioTerminatePrologue[] = {
    0x53, 0x56, 0x8B, 0xF1, 0x8A, 0x86, 0xA4, 0x00, 0x00, 0x00
};
constexpr std::uint8_t kRequestPlayerEngineSoundPrologue[] = {
    0x6A, 0xFF, 0x68, 0xA8, 0xC2, 0x83, 0x00,
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50
};
constexpr std::uint8_t kStartDummyEngineSoundPrologue[] = {
    0x6A, 0xFF, 0x68, 0xC8, 0xC2, 0x83, 0x00,
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50
};
constexpr std::uint8_t kRequestNewSoundPrologue[] = {
    0x56, 0x57, 0x33, 0xF6, 0x0F, 0xBF, 0xC6, 0x6B, 0xC0, 0x74
};
constexpr std::uint8_t kRequestNewSoundHotpatchPadding[] = {
    0x90, 0x90, 0x90, 0x90, 0x90
};
constexpr std::uint8_t kAreBankSoundsPlayingPrologue[] = {
    0x53, 0x56, 0x66, 0x8B, 0x74, 0x24, 0x0C
};
constexpr std::uint8_t kAreEventSoundsPlayingPrologue[] = {
    0x53, 0x55, 0x56, 0x8B, 0x74, 0x24, 0x14
};
constexpr std::uint8_t kAreEventPhysicalSoundsPlayingPrologue[] = {
    0x53, 0x55, 0x56, 0x8B, 0x74, 0x24, 0x18
};
constexpr std::uint8_t kCancelEventSoundsPrologue[] = {
    0x51, 0x56, 0x8D, 0x71, 0x0C
};
constexpr std::uint8_t kCancelEventPhysicalSoundsPrologue[] = {
    0x51, 0x56, 0x8D, 0x71, 0x0C
};
constexpr std::uint8_t kCancelBankSlotSoundsPrologue[] = {
    0x51, 0x56, 0x57, 0x8D, 0x71, 0x0C
};
constexpr std::uint8_t kCancelOwnedSoundsPrologue[] = {
    0x51, 0x56, 0x57, 0x8D, 0x71, 0x0C
};
constexpr std::uint8_t kAudioEngineServicePrologue[] = {
    0x56, 0x57, 0x8B, 0xF1, 0xE9
};
constexpr std::uint8_t kPedSpeechTerminatePrologue[] = {
    0x56, 0x8B, 0xF1, 0xE8, 0x38, 0xE9, 0xFF, 0xFF
};
constexpr std::uint8_t kPedlessSpeechTerminatePrologue[] = {
    0x53, 0x56, 0x8B, 0xF1, 0xE8, 0x77, 0xEB, 0xFF, 0xFF
};
constexpr std::uint8_t kPoliceScannerDestructorPrologue[] = {
    0x6A, 0xFF, 0x68, 0x58, 0xC0, 0x83, 0x00
};
using PlayGunSoundsFn = void(__thiscall*)(
    void* self,
    void* entity,
    std::int16_t emptySfxId,
    std::int16_t farSfxId,
    std::int16_t highPitchSfxId,
    std::int16_t lowPitchSfxId,
    std::int16_t echoSfxId,
    std::int32_t audioEventId,
    float volumeChange,
    float speed1,
    float speed2
);

using ServiceFn = void(__thiscall*)(void* self);
using PlayMinigunFireSoundsFn =
    void(__thiscall*)(void* self, void* entity, std::int32_t audioEventId);
using PlayBulletHitSoundFn = void(__thiscall*)(
    void* self,
    std::int32_t surface,
    const AudioVector* position,
    float angle
);
using SurfaceFlagFn = bool(__thiscall*)(void* self, std::int32_t surface);
using RandomIntFn = std::int32_t(__cdecl*)(std::int32_t, std::int32_t);
using VehicleEngineSoundFn =
    void(__thiscall*)(void*, std::int32_t, float, float);
using VehicleCancelSoundFn = void(__thiscall*)(void*, std::int32_t);
using RequestNewSoundFn = void*(__thiscall*)(void*, void*);
using AreBankSoundsPlayingFn =
    std::int16_t(__thiscall*)(void*, std::int16_t);
using AreEventSoundsPlayingFn =
    std::int16_t(__thiscall*)(void*, std::int16_t, void*);
using AreEventPhysicalSoundsPlayingFn =
    std::int16_t(__thiscall*)(void*, std::int16_t, void*, void*);
using CancelEventSoundsFn =
    void(__thiscall*)(void*, std::int16_t, void*);
using CancelEventPhysicalSoundsFn =
    void(__thiscall*)(void*, std::int16_t, void*, void*);
using CancelBankSlotSoundsFn =
    void(__thiscall*)(void*, std::int16_t, bool);
using CancelOwnedSoundsFn =
    void(__thiscall*)(void*, void*, bool);
using AudioEntityTerminateFn = void(__thiscall*)(void*);
using AudioEntityDestructorFn = void*(__thiscall*)(void*);
using GetPositionRelativeToCameraFn = void(__cdecl*)(AudioVector*, void*);
using VectorRelativeToCameraFn =
    void(__cdecl*)(AudioVector*, const AudioVector*);
using GetDopplerRelativeFrequencyFn =
    float(__cdecl*)(float, float, std::uint32_t, std::uint32_t, float);
using RandomFloatFn = float(__cdecl*)(float, float);
using ResolveProbabilityFn = bool(__cdecl*)(float);
using CanSeeOutsideFn = bool(__cdecl*)();
using AddMessageJumpFn =
    void(__cdecl*)(const char*, std::uint32_t, std::uint16_t, bool);
extern HMODULE gModule;
extern PlayGunSoundsFn gOriginalPlayGunSounds;
extern PlayMinigunFireSoundsFn gOriginalPlayMinigunFireSounds;
extern PlayBulletHitSoundFn gOriginalPlayBulletHitSound;
extern ServiceFn gOriginalAudioEngineService;
extern ServiceFn gOriginalAudioEngineReset;
extern ServiceFn gOriginalVehicleAudioService;
extern ServiceFn gOriginalVehicleAudioTerminate;
extern VehicleEngineSoundFn gOriginalRequestPlayerEngineSound;
extern VehicleEngineSoundFn gOriginalStartDummyEngineSound;
extern VehicleCancelSoundFn gOriginalCancelVehicleEngineSound;
extern RequestNewSoundFn gOriginalRequestNewSound;
extern void* gRequestNewSoundGateway;
extern bool gRequestNewSoundHotpatchInstalled;
extern AreBankSoundsPlayingFn gOriginalAreBankSoundsPlaying;
extern AreEventSoundsPlayingFn gOriginalAreEventSoundsPlaying;
extern AreEventPhysicalSoundsPlayingFn gOriginalAreEventPhysicalSoundsPlaying;
extern CancelEventSoundsFn gOriginalCancelEventSounds;
extern CancelEventPhysicalSoundsFn gOriginalCancelEventPhysicalSounds;
extern CancelBankSlotSoundsFn gOriginalCancelBankSlotSounds;
extern CancelOwnedSoundsFn gOriginalCancelOwnedSounds;
extern AudioEntityTerminateFn gOriginalPedSpeechTerminate;
extern AudioEntityTerminateFn gOriginalPedlessSpeechTerminate;
extern AudioEntityDestructorFn gOriginalPoliceScannerDestructor;

constexpr std::size_t kVehicleEngineSoundCount = 12;
constexpr std::size_t kVehicleAudioStateOffset = 0xA9;
constexpr std::size_t kVehicleHornStateOffset = 0xBE;
constexpr std::size_t kVehicleSirenStateOffset = 0xBF;
constexpr std::size_t kVehicleFastSirenStateOffset = 0xC0;
constexpr std::size_t kVehicleEngineSoundsOffset = 0xE4;
constexpr std::size_t kVehicleDummyBankOffset = 0xDC;
constexpr std::size_t kVehiclePlayerBankOffset = 0xDE;
constexpr std::size_t kVehicleRoadNoiseSoundOffset = 0x160;
constexpr std::size_t kVehicleFlatTireSoundOffset = 0x168;
constexpr std::size_t kVehicleReverseSoundOffset = 0x170;
constexpr std::size_t kVehicleHornSoundOffset = 0x178;
constexpr std::size_t kVehicleSirenSoundOffset = 0x17C;
constexpr std::size_t kVehicleFastSirenSoundOffset = 0x180;
constexpr std::size_t kVehicleSurfaceSoundTypeOffset = 0x15C;
constexpr std::size_t kVehicleSkidSoundOffset = 0x184;
constexpr std::size_t kVehicleSkidInUseOffset = 0x20C;
constexpr std::size_t kVehicleSkidFirstSoundOffset = 0x224;
constexpr std::size_t kVehicleSkidSecondSoundOffset = 0x228;
constexpr std::int16_t kVehicleGeneralBankId = 138;
constexpr std::int16_t kVehicleHornBankId = 74;
constexpr std::int16_t kCopHeliBankId = 13;
constexpr std::int16_t kCollisionBankId = 39;
constexpr std::int16_t kWeaponBankId = 143;
constexpr std::int16_t kBulletHitBankId = 27;
constexpr std::int16_t kRainBankId = 105;
constexpr std::int16_t kCollisionBankSlot = 2;
constexpr std::int16_t kBulletHitBankSlot = 3;
constexpr std::int16_t kWeaponBankSlot = 5;
constexpr std::int16_t kWeatherBankSlot = 6;
constexpr std::int16_t kVehicleDummyBankSlotFirst = 7;
constexpr std::int16_t kVehicleDummyBankSlotLast = 16;
constexpr std::int16_t kVehicleHornBankSlot = 17;
constexpr std::int16_t kCopHeliBankSlot = 18;
constexpr std::int16_t kVehicleGeneralBankSlot = 19;
constexpr std::int16_t kVehiclePlayerEngineBankSlot = 40;
constexpr std::size_t kAeSoundSize = 0x74;
constexpr std::size_t kAeSoundBankSlotOffset = 0x0;
constexpr std::size_t kAeSoundIdOffset = 0x2;
constexpr std::size_t kAeSoundPhysicalEntityOffset = 0x8;
constexpr std::size_t kAeSoundVolumeOffset = 0x14;
constexpr std::size_t kAeSoundRollOffOffset = 0x18;
constexpr std::size_t kAeSoundSpeedOffset = 0x1C;
constexpr std::size_t kAeSoundSpeedVarianceOffset = 0x20;
constexpr std::size_t kAeSoundPositionOffset = 0x24;
constexpr std::size_t kAeSoundDopplerOffset = 0x50;
constexpr std::size_t kAeSoundFrameDelayOffset = 0x54;
constexpr std::size_t kAeSoundFlagsOffset = 0x56;
constexpr std::size_t kAeSoundPlayTimeOffset = 0x5C;
constexpr std::size_t kAeSoundListenerSpeedOffset = 0x64;
constexpr std::size_t kAeSoundStopRequestedOffset = 0x68;
constexpr std::size_t kAeSoundLengthOffset = 0x70;
constexpr std::uint16_t kSoundStartPercentage = 0x20;
constexpr std::uint16_t kSoundCancellable = 0x2;
constexpr std::uint16_t kSoundRequestUpdates = 0x4;
constexpr std::uint16_t kSoundFrontEnd = 0x1;
constexpr std::uint16_t kSoundLifespanTiedToEntity = 0x80;
constexpr std::uint16_t kSoundMusicMastered = 0x40;
constexpr std::uint16_t kSoundUnpausable = 0x10;
constexpr std::int16_t kSpeechBankSlotFirst = 20;
constexpr std::int16_t kSpeechBankSlotLast = 25;
constexpr std::size_t kSpeechBankIdOffset = 0xA6;
constexpr std::int16_t kScannerBankSlotFirst = 33;
constexpr std::int16_t kScannerBankSlotLast = 37;
constexpr std::uintptr_t kScannerCurrentSlotsAddress = 0xB61D10;
constexpr std::uintptr_t kUnregisterSoundAddress = 0x4EF1A0;
constexpr std::uintptr_t kStopSoundAddress = 0x4EF1C0;
constexpr std::uintptr_t kRegisterSoundAddress = 0x4EF820;
constexpr std::uintptr_t kUpdateSoundParametersAddress = 0x4EFF50;
constexpr std::uintptr_t kGetRelativeFrequencyAddress = 0x4EF400;
constexpr std::size_t kHardwareBankLoaderOffset = 0xD98;
constexpr std::size_t kBankSlotSize = 0x12D4;
constexpr std::size_t kBankSlotBankIdOffset = 0x10;
constexpr std::size_t kBankLoaderSlotCountOffset = 0x0C;

struct VehiclePersistentSound {
    std::int32_t proxyType;
    std::size_t pointerOffset;
    std::int16_t bankId;
};

constexpr VehiclePersistentSound kVehiclePersistentSounds[] = {
    {0x20, kVehicleRoadNoiseSoundOffset, kVehicleGeneralBankId},
    {0x21, kVehicleFlatTireSoundOffset, kVehicleGeneralBankId},
    {0x22, kVehicleReverseSoundOffset, kVehicleGeneralBankId},
    {0x23, kVehicleHornSoundOffset, kVehicleHornBankId},
    {0x24, kVehicleSirenSoundOffset, kVehicleHornBankId},
    {0x25, kVehicleFastSirenSoundOffset, kVehicleHornBankId},
    {0x26, kVehicleSkidFirstSoundOffset, kVehicleGeneralBankId},
    {0x27, kVehicleSkidSecondSoundOffset, kVehicleGeneralBankId}
};

using TwinLoopSoundFn = void(__thiscall*)(void* self);
using TwinLoopSwitchFn = bool(__thiscall*)(void* self);
constexpr std::uintptr_t kTwinLoopSwapSoundsAddress = 0x4F2C10;
constexpr std::uintptr_t kTwinLoopDoSoundsSwitchAddress = 0x4F2CA0;

struct VehicleSoundProxy {
    alignas(4) std::array<std::uint8_t, kAeSoundSize> sound{};
    void* owner{};
    std::int32_t soundType{};
    std::int16_t bankId{-1};
    std::uint32_t generation{};
    float playPositionMs{};
    std::uint32_t lastCursorTimeMs{};
    float previousDistance{};
    std::uint32_t previousPositionTimeMs{};
    bool tracksAccelerationCursor{};
    bool active{};
};

struct VehicleCapture {
    void* owner{};
    std::int32_t soundType{};
    std::int16_t bankId{-1};
};

struct DialogueSoundProxy {
    alignas(4) std::array<std::uint8_t, kAeSoundSize> sound{};
    void* owner{};
    std::int16_t bankId{-1};
    std::uint32_t generation{};
    float resolvedSpeed{1.0f};
    float playPositionMs{};
    std::uint32_t lastPlayTimeMs{};
    bool isScanner{};
    bool active{};
};

enum class StatefulSoundModule : std::uint8_t {
    Explosions,
    WeaponEffects,
    VehicleCollisions,
    Characters,
    WorldAmbience
};

struct StatefulSoundProxy {
    alignas(4) std::array<std::uint8_t, kAeSoundSize> sound{};
    void* owner{};
    std::int16_t bankId{-1};
    std::uint32_t generation{};
    float resolvedSpeed{1.0f};
    float playPositionMs{};
    std::uint32_t lastPlayTimeMs{};
    std::uint8_t remainingFrameDelay{};
    StatefulSoundModule module{StatefulSoundModule::WeaponEffects};
    bool started{};
    bool active{};
};
extern std::map<std::uint64_t, VehicleSoundProxy> gVehicleSoundProxies;
extern std::set<void*> gVehicleAudioOwners;
extern std::uint32_t gVehicleOneShotSequence;
extern thread_local VehicleCapture gVehicleCapture;
using DialogueSoundProxyMap = std::map<void*, DialogueSoundProxy>;
extern DialogueSoundProxyMap gDialogueSoundProxies;
extern std::map<std::uint32_t, StatefulSoundProxy> gStatefulSoundProxies;
extern std::uint32_t gStatefulSoundSequence;

} // namespace runtime
