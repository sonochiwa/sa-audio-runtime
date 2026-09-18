#include "modules/modules.h"

namespace runtime {

void ReconcilePersistentVehicleSounds(
    void* self,
    bool replaceOriginal
) {
    bool releasedHorn{};
    bool releasedSiren{};
    bool releasedFastSiren{};
    bool releasedSkid{};
    for (const auto& descriptor : kVehiclePersistentSounds) {
        auto** slot = GetVehicleSoundPointerSlot(
            self,
            descriptor.pointerOffset
        );
        auto* proxy = FindVehicleProxy(
            self,
            descriptor.proxyType
        );
        if (RetireStoppedVehicleProxy(slot, proxy)) {
            continue;
        }
        const bool isProxy =
            proxy &&
            *slot == static_cast<void*>(proxy->sound.data());
        if (!replaceOriginal && isProxy) {
            StopDetachedSound(*slot);
            *slot = nullptr;
            releasedHorn =
                releasedHorn ||
                descriptor.proxyType == 0x23;
            releasedSiren =
                releasedSiren ||
                descriptor.proxyType == 0x24;
            releasedFastSiren =
                releasedFastSiren ||
                descriptor.proxyType == 0x25;
            releasedSkid =
                releasedSkid ||
                descriptor.proxyType == 0x26 ||
                descriptor.proxyType == 0x27;
            if (proxy->active) {
                PublishVehicleStop(*proxy);
            }
        }
    }
    auto* bytes = static_cast<std::uint8_t*>(self);
    if (releasedHorn) {
        bytes[kVehicleHornStateOffset] = 0;
    }
    if (releasedSiren) {
        bytes[kVehicleSirenStateOffset] = 0;
    }
    if (releasedFastSiren) {
        bytes[kVehicleFastSirenStateOffset] = 0;
    }
    if (releasedSkid) {
        *reinterpret_cast<std::int32_t*>(
            bytes + kVehicleSurfaceSoundTypeOffset
        ) = -1;
        *reinterpret_cast<std::int16_t*>(
            bytes + kVehicleSkidInUseOffset
        ) = 0;
    }
}

void UpdateVehicleSkidSwap(void* self, bool replaceOriginal) {
    if (!replaceOriginal) {
        return;
    }
    auto* first = FindVehicleProxy(self, 0x26);
    auto* second = FindVehicleProxy(self, 0x27);
    if (!first || !second || !first->active || !second->active) {
        return;
    }
    if (*GetVehicleSoundPointerSlot(self, kVehicleSkidFirstSoundOffset) !=
            static_cast<void*>(first->sound.data()) ||
        *GetVehicleSoundPointerSlot(self, kVehicleSkidSecondSoundOffset) !=
            static_cast<void*>(second->sound.data())) {
        return;
    }
    auto* skid = static_cast<std::uint8_t*>(self) + kVehicleSkidSoundOffset;
    if (reinterpret_cast<TwinLoopSwitchFn>(
            kTwinLoopDoSoundsSwitchAddress
        )(skid)) {
        reinterpret_cast<TwinLoopSoundFn>(
            kTwinLoopSwapSoundsAddress
        )(skid);
    }
}

void PublishPersistentVehicleSounds(
    void* self,
    bool replaceOriginal
) {
    for (const auto& descriptor : kVehiclePersistentSounds) {
        auto** slot = GetVehicleSoundPointerSlot(
            self,
            descriptor.pointerOffset
        );
        auto* proxy = FindVehicleProxy(
            self,
            descriptor.proxyType
        );
        if (replaceOriginal &&
            *slot &&
            (!proxy ||
             !proxy->active ||
             *slot != static_cast<void*>(proxy->sound.data()))) {
            proxy = AdoptOriginalPersistentVehicleSound(
                self,
                descriptor
            );
        }
        if (!proxy || !proxy->active) {
            continue;
        }
        if (RetireStoppedVehicleProxy(slot, proxy)) {
            continue;
        }
        if (replaceOriginal &&
            *slot == static_cast<void*>(proxy->sound.data())) {
            PublishVehicleUpdate(*proxy);
        } else {
            PublishVehicleStop(*proxy);
        }
    }
}

void UpdateVehicleAccelerationCursor(
    void* self,
    VehicleSoundProxy& proxy
) {
    if (!self || !proxy.active || !proxy.tracksAccelerationCursor) {
        return;
    }

    const auto now =
        *reinterpret_cast<const std::uint32_t*>(kGameTimeMsAddress);
    if (proxy.lastCursorTimeMs == 0) {
        proxy.lastCursorTimeMs = now;
        return;
    }

    const auto elapsed = now - proxy.lastCursorTimeMs;
    proxy.lastCursorTimeMs = now;
    const auto speed = std::max(
        ReadProxyField<float>(proxy, kAeSoundSpeedOffset),
        0.05f
    );
    const auto length = std::max<std::int16_t>(
        ReadProxyField<std::int16_t>(
            proxy,
            kAeSoundLengthOffset
        ),
        1
    );
    proxy.playPositionMs = std::fmod(
        proxy.playPositionMs + static_cast<float>(elapsed) * speed,
        static_cast<float>(length)
    );

    auto* bytes = static_cast<std::uint8_t*>(self);
    auto* currentPosition =
        reinterpret_cast<std::int16_t*>(bytes + 0x148);
    auto* lastPosition =
        reinterpret_cast<std::int16_t*>(bytes + 0x14A);
    *lastPosition = *currentPosition;
    *currentPosition =
        static_cast<std::int16_t>(proxy.playPositionMs);
}

void __fastcall HookVehicleAudioService(void* self, void*) {
    gVehicleAudioOwners.insert(self);
    const bool replaceEngines = IsVehicleEngineRendererEnabled();
    const bool replaceEffects = IsVehicleEffectRendererEnabled();
    ReconcileVehicleEngineOwnership(self, replaceEngines);
    ReconcilePersistentVehicleSounds(self, replaceEffects);

    if (replaceEngines) {
        auto* proxy = FindVehicleProxy(self, 4);
        if (proxy &&
            *GetVehicleEngineSoundSlot(self, 4) ==
                static_cast<void*>(proxy->sound.data())) {
            UpdateVehicleAccelerationCursor(self, *proxy);
        }
    }

    gOriginalVehicleAudioService(self);

    UpdateVehicleSkidSwap(self, replaceEffects);

    for (std::int32_t soundType = 0;
         soundType < static_cast<std::int32_t>(kVehicleEngineSoundCount);
         ++soundType) {
        auto* proxy = FindVehicleProxy(self, soundType);
        auto* slot = *GetVehicleEngineSoundSlot(self, soundType);
        if (replaceEngines &&
            slot &&
            (!proxy ||
             !proxy->active ||
             slot != static_cast<void*>(proxy->sound.data()))) {
            proxy = AdoptOriginalVehicleEngineSound(
                self,
                soundType
            );
        }
        if (!proxy || !proxy->active) {
            continue;
        }
        auto** currentSlot = GetVehicleEngineSoundSlot(
            self,
            soundType
        );
        if (RetireStoppedVehicleProxy(currentSlot, proxy)) {
            continue;
        }
        slot = *currentSlot;
        if (replaceEngines &&
            slot == static_cast<void*>(proxy->sound.data())) {
            PublishVehicleUpdate(*proxy);
        } else {
            PublishVehicleStop(*proxy);
        }
    }
    PublishPersistentVehicleSounds(self, replaceEffects);
}

void __fastcall HookVehicleAudioTerminate(void* self, void*) {
    gVehicleAudioOwners.erase(self);
    gOriginalVehicleAudioTerminate(self);
    for (auto proxy = gVehicleSoundProxies.begin();
         proxy != gVehicleSoundProxies.end();) {
        if (proxy->second.owner != self) {
            ++proxy;
            continue;
        }
        if (proxy->second.active) {
            PublishVehicleStop(proxy->second);
        }
        proxy = gVehicleSoundProxies.erase(proxy);
    }
}

} // namespace runtime
