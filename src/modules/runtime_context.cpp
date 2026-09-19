#include "modules/modules.h"

namespace runtime {

HMODULE gModule{};
PlayGunSoundsFn gOriginalPlayGunSounds{};
PlayMinigunFireSoundsFn gOriginalPlayMinigunFireSounds{};
PlayBulletHitSoundFn gOriginalPlayBulletHitSound{};
ServiceFn gOriginalAudioEngineService{};
ServiceFn gOriginalAudioEngineReset{};
ServiceFn gOriginalVehicleAudioService{};
ServiceFn gOriginalVehicleAudioTerminate{};
VehicleEngineSoundFn gOriginalRequestPlayerEngineSound{};
VehicleEngineSoundFn gOriginalStartDummyEngineSound{};
VehicleCancelSoundFn gOriginalCancelVehicleEngineSound{};
RequestNewSoundFn gOriginalRequestNewSound{};
void* gRequestNewSoundGateway{};
bool gRequestNewSoundHotpatchInstalled{};
AreBankSoundsPlayingFn gOriginalAreBankSoundsPlaying{};
AreEventSoundsPlayingFn gOriginalAreEventSoundsPlaying{};
AreEventPhysicalSoundsPlayingFn gOriginalAreEventPhysicalSoundsPlaying{};
CancelEventSoundsFn gOriginalCancelEventSounds{};
CancelEventPhysicalSoundsFn gOriginalCancelEventPhysicalSounds{};
CancelBankSlotSoundsFn gOriginalCancelBankSlotSounds{};
CancelOwnedSoundsFn gOriginalCancelOwnedSounds{};
AudioEntityTerminateFn gOriginalPedSpeechTerminate{};
AudioEntityTerminateFn gOriginalPedlessSpeechTerminate{};
AudioEntityDestructorFn gOriginalPoliceScannerDestructor{};

std::map<std::uint64_t, VehicleSoundProxy> gVehicleSoundProxies;
std::set<void*> gVehicleAudioOwners;
std::uint32_t gVehicleOneShotSequence{};
thread_local VehicleCapture gVehicleCapture{};
DialogueSoundProxyMap gDialogueSoundProxies;
std::map<std::uint32_t, StatefulSoundProxy> gStatefulSoundProxies;
std::uint32_t gStatefulSoundSequence{};

} // namespace runtime
