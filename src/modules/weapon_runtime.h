#pragma once

#include "modules/prelude.h"

namespace runtime {

AudioJob BuildGunAudioJob( void* self, void* entity, std::int16_t emptySfxId, std::int16_t farSfxId, std::int16_t highPitchSfxId, std::int16_t lowPitchSfxId, std::int16_t echoSfxId, std::int32_t audioEventId, float volumeChange, float speed1, float speed2 );
void __fastcall HookPlayGunSounds( void* self, void*, void* entity, std::int16_t emptySfxId, std::int16_t farSfxId, std::int16_t highPitchSfxId, std::int16_t lowPitchSfxId, std::int16_t echoSfxId, std::int32_t audioEventId, float volumeChange, float speed1, float speed2 );
void __fastcall HookPlayMinigunFireSounds( void* self, void*, void* entity, std::int32_t audioEventId );
bool HasSurfaceFlag(std::uintptr_t address, std::int32_t surface);
void __fastcall HookPlayBulletHitSound( void* self, void*, std::int32_t surface, const AudioVector* position, float angle );

} // namespace runtime
