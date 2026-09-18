#pragma once

#include "modules/prelude.h"

namespace runtime {

void __fastcall HookRequestPlayerEngineSound( void* self, void*, std::int32_t soundType, float speed, float volume );
void __fastcall HookStartDummyEngineSound( void* self, void*, std::int32_t soundType, float speed, float volume );
void ReconcileVehicleEngineOwnership(void* self, bool replaceOriginal);
VehicleSoundProxy* AdoptOriginalVehicleEngineSound( void* self, std::int32_t soundType );
VehicleSoundProxy* AdoptOriginalPersistentVehicleSound( void* self, const VehiclePersistentSound& descriptor );
void ReconcilePersistentVehicleSounds( void* self, bool replaceOriginal );
void UpdateVehicleSkidSwap(void* self, bool replaceOriginal);
void PublishPersistentVehicleSounds( void* self, bool replaceOriginal );
void UpdateVehicleAccelerationCursor( void* self, VehicleSoundProxy& proxy );
void __fastcall HookVehicleAudioService(void* self, void*);
void __fastcall HookVehicleAudioTerminate(void* self, void*);

} // namespace runtime
