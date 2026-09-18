#include "modules/modules.h"

namespace runtime {

void __fastcall HookRequestPlayerEngineSound(
    void* self,
    void*,
    std::int32_t soundType,
    float speed,
    float volume
) {
    if (!IsVehicleEngineRendererEnabled()) {
        gOriginalRequestPlayerEngineSound(
            self,
            soundType,
            speed,
            volume
        );
        return;
    }
    const auto bankId = ReadVehicleBank(
        self,
        soundType == 1 || soundType == 2
            ? kVehicleDummyBankOffset
            : kVehiclePlayerBankOffset
    );
    BeginVehicleCapture(self, soundType, bankId);
    gOriginalRequestPlayerEngineSound(
        self,
        soundType,
        speed,
        volume
    );
    EndVehicleCapture();
}

void __fastcall HookStartDummyEngineSound(
    void* self,
    void*,
    std::int32_t soundType,
    float speed,
    float volume
) {
    if (!IsVehicleEngineRendererEnabled()) {
        gOriginalStartDummyEngineSound(
            self,
            soundType,
            speed,
            volume
        );
        return;
    }
    BeginVehicleCapture(
        self,
        soundType,
        ReadVehicleBank(self, kVehicleDummyBankOffset)
    );
    gOriginalStartDummyEngineSound(
        self,
        soundType,
        speed,
        volume
    );
    EndVehicleCapture();
}

void ReconcileVehicleEngineOwnership(void* self, bool replaceOriginal) {
    bool releasedProxy{};
    for (std::int32_t soundType = 0;
         soundType < static_cast<std::int32_t>(kVehicleEngineSoundCount);
         ++soundType) {
        auto** slot = GetVehicleEngineSoundSlot(self, soundType);
        auto* proxy = FindVehicleProxy(self, soundType);
        if (RetireStoppedVehicleProxy(slot, proxy)) {
            continue;
        }
        const bool isProxy =
            proxy && *slot == static_cast<void*>(proxy->sound.data());
        if (!replaceOriginal && isProxy) {
            gOriginalCancelVehicleEngineSound(self, soundType);
            releasedProxy = true;
            if (proxy && proxy->active) {
                PublishVehicleStop(*proxy);
            }
        }
    }
    if (releasedProxy) {
        *reinterpret_cast<std::uint8_t*>(
            static_cast<std::uint8_t*>(self) +
            kVehicleAudioStateOffset
        ) = 0;
    }
}

VehicleSoundProxy* AdoptOriginalVehicleEngineSound(
    void* self,
    std::int32_t soundType
) {
    if (soundType < 0 ||
        soundType >= static_cast<std::int32_t>(kVehicleEngineSoundCount)) {
        return nullptr;
    }

    auto** slot = GetVehicleEngineSoundSlot(self, soundType);
    if (!*slot) {
        return nullptr;
    }
    const auto* originalBytes = static_cast<const std::uint8_t*>(*slot);
    const auto bankSlot = *reinterpret_cast<const std::int16_t*>(
        originalBytes + kAeSoundBankSlotOffset
    );
    const auto soundId = *reinterpret_cast<const std::int16_t*>(
        originalBytes + kAeSoundIdOffset
    );
    const bool bicycleChainClang =
        soundType == 3 &&
        bankSlot == kCollisionBankSlot &&
        (soundId == 5 || soundId == 7 || soundId == 8 || soundId == 9);
    if (bicycleChainClang) {
        return nullptr;
    }
    if (auto* existing = FindVehicleProxy(self, soundType);
        existing &&
        *slot == static_cast<void*>(existing->sound.data())) {
        return existing;
    }

    std::array<std::uint8_t, kAeSoundSize> originalSound{};
    std::memcpy(
        originalSound.data(),
        *slot,
        originalSound.size()
    );
    gOriginalCancelVehicleEngineSound(self, soundType);

    auto& proxy = gVehicleSoundProxies[
        GetVehicleProxyKey(self, soundType)
    ];
    std::memcpy(
        proxy.sound.data(),
        originalSound.data(),
        proxy.sound.size()
    );
    proxy.owner = self;
    proxy.soundType = soundType;
    proxy.bankId = ResolvePersistentVehicleBank(
        self,
        originalSound.data(),
        ReadVehicleBank(
            self,
            soundType == 1 || soundType == 2
                ? kVehicleDummyBankOffset
                : kVehiclePlayerBankOffset
        )
    );
    ++proxy.generation;
    if (proxy.generation == 0) {
        ++proxy.generation;
    }
    proxy.playPositionMs = 0.0f;
    proxy.lastCursorTimeMs =
        *reinterpret_cast<const std::uint32_t*>(kGameTimeMsAddress);
    if (auto* entity = *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(self) + 4
        )) {
        proxy.previousDistance = VectorMagnitude(
            PositionRelativeToCamera(ReadPlaceablePosition(entity))
        );
        proxy.previousPositionTimeMs = proxy.lastCursorTimeMs;
    } else {
        proxy.previousDistance = 0.0f;
        proxy.previousPositionTimeMs = 0;
    }
    const auto flags = ReadProxyField<std::uint16_t>(
        proxy,
        kAeSoundFlagsOffset
    );
    proxy.tracksAccelerationCursor =
        soundType == 4 &&
        (flags & kSoundStartPercentage) != 0;
    proxy.active = true;
    if (proxy.tracksAccelerationCursor) {
        const std::int16_t accelerationLength = 1000;
        std::memcpy(
            proxy.sound.data() + kAeSoundLengthOffset,
            &accelerationLength,
            sizeof(accelerationLength)
        );
        const auto playTime = std::clamp(
            ReadProxyField<std::int16_t>(
                proxy,
                kAeSoundPlayTimeOffset
            ),
            static_cast<std::int16_t>(0),
            static_cast<std::int16_t>(100)
        );
        proxy.playPositionMs =
            static_cast<float>(playTime) * 10.0f;
    }
    *slot = proxy.sound.data();
    return &proxy;
}

VehicleSoundProxy* AdoptOriginalPersistentVehicleSound(
    void* self,
    const VehiclePersistentSound& descriptor
) {
    auto** slot = GetVehicleSoundPointerSlot(
        self,
        descriptor.pointerOffset
    );
    if (!*slot) {
        return nullptr;
    }
    if (auto* existing = FindVehicleProxy(
            self,
            descriptor.proxyType
        );
        existing &&
        *slot == static_cast<void*>(existing->sound.data())) {
        return existing;
    }

    std::array<std::uint8_t, kAeSoundSize> originalSound{};
    std::memcpy(
        originalSound.data(),
        *slot,
        originalSound.size()
    );
    StopDetachedSound(*slot);
    *slot = nullptr;

    auto& proxy = gVehicleSoundProxies[
        GetVehicleProxyKey(self, descriptor.proxyType)
    ];
    std::memcpy(
        proxy.sound.data(),
        originalSound.data(),
        proxy.sound.size()
    );
    proxy.owner = self;
    proxy.soundType = descriptor.proxyType;
    proxy.bankId = ResolvePersistentVehicleBank(
        self,
        originalSound.data(),
        descriptor.bankId
    );
    ++proxy.generation;
    if (proxy.generation == 0) {
        ++proxy.generation;
    }
    proxy.playPositionMs = 0.0f;
    proxy.lastCursorTimeMs =
        *reinterpret_cast<const std::uint32_t*>(kGameTimeMsAddress);
    if (auto* entity = *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(self) + 4
        )) {
        proxy.previousDistance = VectorMagnitude(
            PositionRelativeToCamera(ReadPlaceablePosition(entity))
        );
        proxy.previousPositionTimeMs = proxy.lastCursorTimeMs;
    } else {
        proxy.previousDistance = 0.0f;
        proxy.previousPositionTimeMs = 0;
    }
    proxy.tracksAccelerationCursor = false;
    proxy.active = true;
    *slot = proxy.sound.data();
    return &proxy;
}

} // namespace runtime
