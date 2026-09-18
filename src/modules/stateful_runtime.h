#pragma once

#include "modules/prelude.h"

namespace runtime {

AudioJob BuildDialogueJob( const std::uint8_t* sound, std::uintptr_t sourceKey, std::uint32_t generation, std::int16_t bankId, AudioJobType type );
void NotifyDialogueFinished(DialogueSoundProxy& proxy);
void StopDialogue(DialogueSoundProxy& proxy);
void RemoveTerminatedDialogueProxy(void* owner);

struct DialogueProxyIdentity {
    void* owner{};
    std::uintptr_t sourceKey{};
    std::uint32_t generation{};
};
DialogueProxyIdentity GetDialogueProxyIdentity( void* owner, const DialogueSoundProxy& proxy );
DialogueSoundProxyMap::iterator FindDialogueProxy( const DialogueProxyIdentity& identity );
DialogueSoundProxyMap::iterator EraseDialogueProxy( const DialogueProxyIdentity& identity );
void __fastcall HookPedSpeechTerminate(void* self, void*);
void __fastcall HookPedlessSpeechTerminate(void* self, void*);
void* __fastcall HookPoliceScannerDestructor(void* self, void*);
void ServiceDialogueProxies();
void BeginVehicleCapture( void* owner, std::int32_t soundType, std::int16_t bankId );
void EndVehicleCapture();
bool IsDialogueRendererEnabled();
bool IsScannerRendererEnabled();
bool IsEffectsRendererEnabled();
bool IsAudioRuntimePaused();
bool ResolveLoadedBank(std::int16_t bankSlot, std::int16_t& bankId);
bool IsStatefulModuleEnabled(StatefulSoundModule module);
bool ClassifyStatefulSound( void* owner, std::int16_t bankSlot, std::int16_t bankId, std::int16_t soundId, std::int32_t eventId, StatefulSoundModule& module );
void StopStatefulSound(StatefulSoundProxy& proxy);
void FinishStatefulSound(StatefulSoundProxy& proxy);
bool HandleStatefulCompletion(const AudioCompletion& completion);
void ServiceStatefulSounds();
bool StatefulSoundMatches( const StatefulSoundProxy& proxy, void* owner, std::int32_t eventId, void* physicalEntity, std::int16_t bankSlot );
bool HasStatefulSound( void* owner, std::int16_t eventId, void* physicalEntity );
bool HasStatefulSoundInBank(std::int16_t bankSlot);
void CancelStatefulSounds( void* owner, std::int32_t eventId, void* physicalEntity, std::int16_t bankSlot, bool forget );
std::int16_t __fastcall HookAreEventSoundsPlaying( void* self, void*, std::int16_t eventId, void* owner );
std::int16_t __fastcall HookAreBankSoundsPlaying( void* self, void*, std::int16_t bankSlot );
std::int16_t __fastcall HookAreEventPhysicalSoundsPlaying( void* self, void*, std::int16_t eventId, void* owner, void* physicalEntity );
void __fastcall HookCancelEventSounds( void* self, void*, std::int16_t eventId, void* owner );
void __fastcall HookCancelEventPhysicalSounds( void* self, void*, std::int16_t eventId, void* owner, void* physicalEntity );
void __fastcall HookCancelBankSlotSounds( void* self, void*, std::int16_t bankSlot, bool fullStop );
void __fastcall HookCancelOwnedSounds( void* self, void*, void* owner, bool fullStop );
void* __fastcall HookRequestNewSound( void* self, void*, void* sound );

} // namespace runtime
