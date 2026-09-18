#pragma once

#include "modules/prelude.h"

namespace runtime {

std::uint64_t GetVehicleProxyKey( void* owner, std::int32_t soundType );
void** GetVehicleEngineSoundSlot( void* owner, std::int32_t soundType );
void** GetVehicleSoundPointerSlot( void* owner, std::size_t offset );
void StopDetachedSound(void* sound);
std::int16_t ReadVehicleBank(void* owner, std::size_t offset);
std::int16_t ResolvePersistentVehicleBank( void* owner, const std::uint8_t* sound, std::int16_t fallback );
VehicleSoundProxy* FindVehicleProxy( void* owner, std::int32_t soundType );
bool IsVehicleEngineRendererEnabled();
bool IsVehicleEffectRendererEnabled();

template<typename T>
T ReadProxyField(
    const VehicleSoundProxy& proxy,
    std::size_t offset
) {
    T value{};
    std::memcpy(&value, proxy.sound.data() + offset, sizeof(value));
    return value;
}
bool HandleVehicleCompletion(const AudioCompletion& completion);
void PublishVehicleStop(VehicleSoundProxy& proxy);
bool RetireStoppedVehicleProxy( void** slot, VehicleSoundProxy* proxy );
void PublishVehicleUpdate(VehicleSoundProxy& proxy);

template<typename T>
T ReadSoundField(const std::uint8_t* sound, std::size_t offset) {
    T value{};
    std::memcpy(&value, sound + offset, sizeof(value));
    return value;
}

} // namespace runtime
