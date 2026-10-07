#pragma once

#include <cstddef>
#include <cstdint>

// Debug autotest only: the game functions and data the test scene drives.
// Each function is checked against its first bytes before the test starts;
// "??" skips a byte (the relocated bodies of the Hoodlum executable start
// with a jump whose target differs between builds).
namespace autotest {

struct CodeSite {
    const char* name;
    std::uintptr_t address;
    const char* bytes;
    // Another plugin may already have detoured a hooked function.
    bool detourAllowed;
};

// Hooked by the test: CPad::UpdatePads (push esi; push edi; jmp),
// CGame::Process (sub esp,0Ch) and CTheScripts::Process (mov al,[...]).
inline constexpr CodeSite kUpdatePads{"CPad::UpdatePads", 0x541DD0, "56 57", true};
inline constexpr CodeSite kGameProcess{"CGame::Process", 0x53BEE0, "83 EC 0C 53 56 57", true};
inline constexpr CodeSite kScriptsProcess{
    "CTheScripts::Process", 0x46A000, "A0 ?? ?? ?? ?? 83 EC 10", true};

// FindPlayerPed(int32) and FindPlayerWanted(int32): mov eax,[esp+4].
inline constexpr CodeSite kFindPlayerPed{
    "FindPlayerPed", 0x56E210, "8B 44 24 04 85 C0 7D 07", false};
inline constexpr CodeSite kFindPlayerWanted{
    "FindPlayerWanted", 0x56E230, "8B 44 24 04 85 C0 7D 07", false};
// CCheat::VehicleCheat(int32 model) returns the vehicle it placed in front of
// the player; CCarEnterExit::SetPedInCarDirect(CPed*, CVehicle*, int32 seat,
// bool driver); CPopulation::AddPed(ePedType, int32 model, const CVector&,
// bool wander). All three: nop; jmp to the relocated body.
inline constexpr CodeSite kVehicleCheat{"CCheat::VehicleCheat", 0x43A0B0, "90 E9", false};
inline constexpr CodeSite kSetPedInCarDirect{
    "CCarEnterExit::SetPedInCarDirect", 0x650280, "90 E9", false};
inline constexpr CodeSite kAddPed{"CPopulation::AddPed", 0x612710, "90 E9", false};
// CWorld::Remove(CEntity*): push esi; mov esi,[esp+8].
inline constexpr CodeSite kWorldRemove{"CWorld::Remove", 0x563280, "56 8B 74 24 08 8B 06", false};
// CEntity::CleanUpOldReference(CEntity** reference), on the referenced entity:
// mov eax,[ecx+24h].
inline constexpr CodeSite kCleanUpOldReference{
    "CEntity::CleanUpOldReference", 0x571A00, "8B 41 24 83 C1 24", false};
// CStreaming::LoadScene(const CVector*) and CWorld::FindGroundZForCoord(x, y).
inline constexpr CodeSite kLoadScene{
    "CStreaming::LoadScene", 0x40EB70, "83 EC 1C 55 8B 6C 24 24", false};
inline constexpr CodeSite kFindGroundZ{
    "CWorld::FindGroundZForCoord", 0x569660, "83 EC 38 8B 44 24 3C", false};
// CCamera::Fade(float, uint16) and
// CCamera::SetCameraDirectlyBehindForFollowPed_CamOnAString(), on TheCamera.
inline constexpr CodeSite kCameraFade{"CCamera::Fade", 0x50AC20, "E9", false};
inline constexpr CodeSite kCameraBehind{
    "CCamera::SetCameraDirectlyBehindForFollowPed_CamOnAString", 0x50BD40,
    "56 8B F1 6A FF C6 46 1A 01", false};
// CExplosion::AddExplosion(CEntity* victim, CEntity* creator, type, CVector,
// uint32 lifetime, uint8 sound, float shake, uint8 visible).
inline constexpr CodeSite kAddExplosion{
    "CExplosion::AddExplosion", 0x736A50, "83 EC 1C 53 55 56 57", false};
// CFireManager::StartFire(CVector, float size, uint8, CEntity* creator,
// uint32 burnTime, int8 generations, uint8), on gFireManager.
inline constexpr CodeSite kStartFire{
    "CFireManager::StartFire", 0x539F00, "51 55 8B 6C 24 10 56 57", false};
// CWeather::ForceWeatherNow(int16), CClock::SetGameClock(uint8 h, m, day) and
// CWanted::SetWantedLevel(int32).
inline constexpr CodeSite kForceWeatherNow{"CWeather::ForceWeatherNow", 0x72A4F0, "E9", false};
inline constexpr CodeSite kSetGameClock{"CClock::SetGameClock", 0x52D150, "A1 84 CB B7 00", false};
inline constexpr CodeSite kSetWantedLevel{
    "CWanted::SetWantedLevel", 0x562470, "A0 71 91 96 00", false};
// CPed::Say(uint16 context, uint32 delay, float probability, bool, bool, bool).
inline constexpr CodeSite kPedSay{"CPed::Say", 0x5EFFE0, "8B 44 24 04 66 85 C0", false};
// CAEPedAudioEntity::AddAudioEvent(event, float volume, float speed, CPhysical*,
// surface, int32, uint32): push -1; jmp.
inline constexpr CodeSite kPedAudioAddEvent{
    "CAEPedAudioEntity::AddAudioEvent", 0x4E2BB0, "6A FF E9", false};
// CAEWeaponAudioEntity::WeaponFire(eWeaponType, CPhysical*, eAudioEvents).
inline constexpr CodeSite kWeaponFire{
    "CAEWeaponAudioEntity::WeaponFire", 0x504F80, "8B 44 24 08 85 C0 56 8B F1", false};
// CAudioEngine: ReportFrontendAudioEvent(event, float volume, float speed),
// ReportBulletHit(CEntity*, surface, const CVector&, float angle), Reset(),
// PreloadMissionAudio(uint8 slot, int32 id), GetMissionAudioLoadingStatus(uint8)
// and PlayLoadedMissionAudio(uint8): add ecx,<entity>; jmp, except Reset.
inline constexpr CodeSite kReportFrontend{
    "CAudioEngine::ReportFrontendAudioEvent", 0x506EA0, "81 C1 B4 00 00 00", false};
inline constexpr CodeSite kReportBulletHit{
    "CAudioEngine::ReportBulletHit", 0x506EC0, "81 C1 BC 04 00 00", false};
inline constexpr CodeSite kAudioEngineReset{
    "CAudioEngine::Reset", 0x507A90, "56 8B F1 B9 B0 2C B6 00", true};
inline constexpr CodeSite kPreloadMissionAudio{
    "CAudioEngine::PreloadMissionAudio", 0x507290, "81 C1 A0 02 00 00", false};
inline constexpr CodeSite kMissionAudioStatus{
    "CAudioEngine::GetMissionAudioLoadingStatus", 0x5072A0, "81 C1 A0 02 00 00", false};
inline constexpr CodeSite kPlayMissionAudio{
    "CAudioEngine::PlayLoadedMissionAudio", 0x5072B0, "81 C1 A0 02 00 00", false};
// CAESound::CalculateVolume() and CAEAudioHardware::GetSoundHeadroom(int16
// sound, int16 bank slot): the game's own listener volume of a sound.
inline constexpr CodeSite kCalculateVolume{
    "CAESound::CalculateVolume", 0x4EFA10, "83 EC 10 56 8B F1 F6 46 56 01", false};
inline constexpr CodeSite kGetSoundHeadroom{
    "CAEAudioHardware::GetSoundHeadroom", 0x4D8E30, "8B 89 98 0D 00 00", false};

inline constexpr const CodeSite* kSites[] = {
    &kUpdatePads,          &kGameProcess,         &kScriptsProcess,     &kFindPlayerPed,
    &kFindPlayerWanted,    &kVehicleCheat,        &kSetPedInCarDirect,  &kAddPed,
    &kWorldRemove,         &kCleanUpOldReference, &kLoadScene,          &kFindGroundZ,
    &kCameraFade,          &kCameraBehind,        &kAddExplosion,       &kStartFire,
    &kForceWeatherNow,     &kSetGameClock,        &kSetWantedLevel,     &kPedSay,
    &kPedAudioAddEvent,    &kWeaponFire,          &kReportFrontend,     &kReportBulletHit,
    &kAudioEngineReset,    &kPreloadMissionAudio, &kMissionAudioStatus, &kPlayMissionAudio,
    &kCalculateVolume,     &kGetSoundHeadroom};

// gGameState and the states the start-up skip passes through.
constexpr std::uintptr_t kGameState = 0xC8D4C0;
constexpr std::int32_t kGameStatePlayingLogo = 2;
constexpr std::int32_t kGameStateTitle = 3;
constexpr std::int32_t kGameStatePlayingIntro = 4;
constexpr std::int32_t kGameStateFrontendLoading = 5;
constexpr std::int32_t kGameStateFrontendIdle = 7;
// FrontEndMenuManager.m_bActivateMenuNextFrame and m_bMenuActive.
constexpr std::uintptr_t kMenuActivateNextFrame = 0xBA677B;
constexpr std::uintptr_t kMenuActive = 0xBA67A4;
// RsGlobal.quit: the main loop ends and the game shuts down.
constexpr std::uintptr_t kRsGlobalQuit = 0xC17050;
// CTimer::m_snTimeInMilliseconds (game time).
constexpr std::uintptr_t kGameTime = 0xB7CB84;
// CCamera::Fade direction that fades the screen in.
constexpr std::uint16_t kFadeIn = 0;
// CTimer::m_UserPause, what the pause menu sets.
constexpr std::uintptr_t kUserPause = 0xB7CB49;
constexpr std::uintptr_t kTheCamera = 0xB6F028;
constexpr std::uintptr_t kAudioEngine = 0xB6BC90;
constexpr std::uintptr_t kAudioHardware = 0xB5F8B8;
constexpr std::uintptr_t kFireManager = 0xB71F80;
// AESoundManager and its size: the game's own sound slots live inside it.
constexpr std::uintptr_t kSoundManager = 0xB62CB0;
constexpr std::size_t kSoundManagerSize = 0x8CBC;
// CPools::ms_pVehiclePool: objects, byte map (bit 7 = free), size; elements
// are 0xA18 bytes.
constexpr std::uintptr_t kVehiclePool = 0xB74494;
constexpr std::size_t kVehiclePoolElementSize = 0xA18;

// CPad::Pads[0].NewState, the state the game reads after UpdatePads.
constexpr std::uintptr_t kPlayerPad = 0xB73458;
constexpr std::size_t kPadLeftStickX = 0x00;
constexpr std::size_t kPadRightShoulder1 = 0x0C;
constexpr std::size_t kPadButtonSquare = 0x1C;
constexpr std::size_t kPadButtonCross = 0x20;
constexpr std::size_t kPadShockButtonL = 0x24;

// The deleting destructor is the first slot of every entity vtable.
constexpr std::size_t kEntityDeleteSlot = 0;
// CEntity::m_nStatus: bits 3-7 of the byte at 0x36; 4 is STATUS_ABANDONED.
constexpr std::size_t kEntityStatusByte = 0x36;
constexpr std::uint8_t kEntityStatusKeep = 0x07;
constexpr std::uint8_t kStatusAbandoned = 4 << 3;
// CEntity::Teleport(CVector, bool) vtable slot; CPlaceable::m_matrix with the
// forward vector at 0x10 and the position at 0x30.
constexpr std::size_t kEntityTeleportSlot = 14;
constexpr std::size_t kPlaceableMatrix = 0x14;
constexpr std::size_t kMatrixForward = 0x10;
constexpr std::size_t kMatrixPosition = 0x30;
// CPed: m_pedAudio, m_weaponAudio, m_fHealth, m_pVehicle.
constexpr std::size_t kPedAudio = 0x138;
constexpr std::size_t kPedWeaponAudio = 0x394;
constexpr std::size_t kPedHealth = 0x540;
constexpr std::size_t kPedVehicle = 0x58C;
// CVehicle: m_vehicleAudio, m_pDriver, m_nCreatedBy (2 = mission vehicle,
// which the game does not remove).
constexpr std::size_t kVehicleAudio = 0x138;
constexpr std::size_t kVehicleDriver = 0x460;
constexpr std::size_t kVehicleCreatedBy = 0x4A4;
constexpr std::uint32_t kMissionVehicle = 2;
// CAEVehicleAudioEntity::m_SurfaceSoundType (the skid twin loop's sound) and
// m_RoadNoiseSoundType (plugin-sdk VALIDATE_OFFSET 0x156 and 0x15C).
constexpr std::size_t kVehicleAudioSkidType = 0x156;
constexpr std::size_t kVehicleAudioRoadNoiseType = 0x15C;
// CAESound::m_fFinalVolume (listener volume) and m_fSoundHeadRoom.
constexpr std::size_t kSoundListenerVolume = 0x60;
constexpr std::size_t kSoundHeadroom = 0x6C;

} // namespace autotest
