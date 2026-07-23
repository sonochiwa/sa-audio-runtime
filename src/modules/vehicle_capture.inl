std::uint64_t GetVehicleProxyKey(
    void* owner,
    std::int32_t soundType
) {
    return
        (static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(owner)
        ) << 8) |
        static_cast<std::uint8_t>(soundType);
}

void** GetVehicleEngineSoundSlot(
    void* owner,
    std::int32_t soundType
) {
    return reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(owner) +
        kVehicleEngineSoundsOffset +
        static_cast<std::size_t>(soundType) * 8 +
        4
    );
}

void** GetVehicleSoundPointerSlot(
    void* owner,
    std::size_t offset
) {
    return reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(owner) + offset
    );
}

void StopDetachedSound(void* sound) {
    if (!sound) {
        return;
    }
    auto* bytes = static_cast<std::uint8_t*>(sound);
    auto* flags = reinterpret_cast<std::uint16_t*>(
        bytes + kAeSoundFlagsOffset
    );
    *flags &= static_cast<std::uint16_t>(~kSoundRequestUpdates);
    *reinterpret_cast<std::int16_t*>(
        bytes + kAeSoundStopRequestedOffset
    ) = 1;
}

std::int16_t ReadVehicleBank(void* owner, std::size_t offset) {
    return *reinterpret_cast<const std::int16_t*>(
        static_cast<const std::uint8_t*>(owner) + offset
    );
}

std::int16_t ResolvePersistentVehicleBank(
    void* owner,
    const std::uint8_t* sound,
    std::int16_t fallback
) {
    std::int16_t bankSlot{};
    std::memcpy(
        &bankSlot,
        sound + kAeSoundBankSlotOffset,
        sizeof(bankSlot)
    );
    if (bankSlot == kVehicleGeneralBankSlot) {
        return kVehicleGeneralBankId;
    }
    if (bankSlot == kCollisionBankSlot) {
        return kCollisionBankId;
    }
    if (bankSlot == kBulletHitBankSlot) {
        return kBulletHitBankId;
    }
    if (bankSlot == kWeaponBankSlot) {
        return kWeaponBankId;
    }
    if (bankSlot == kWeatherBankSlot) {
        return kRainBankId;
    }
    if (bankSlot == kVehicleHornBankSlot) {
        return kVehicleHornBankId;
    }
    if (bankSlot == kCopHeliBankSlot) {
        return kCopHeliBankId;
    }
    if (bankSlot == kVehiclePlayerEngineBankSlot) {
        const auto playerBank = ReadVehicleBank(
            owner,
            kVehiclePlayerBankOffset
        );
        if (playerBank >= 0) {
            return playerBank;
        }
    }
    if (bankSlot >= kVehicleDummyBankSlotFirst &&
        bankSlot <= kVehicleDummyBankSlotLast) {
        const auto dummySlot = ReadVehicleBank(owner, 0xE0);
        const auto dummyBank = ReadVehicleBank(
            owner,
            kVehicleDummyBankOffset
        );
        if (bankSlot == dummySlot && dummyBank >= 0) {
            return dummyBank;
        }
    }
    return fallback;
}

VehicleSoundProxy* FindVehicleProxy(
    void* owner,
    std::int32_t soundType
) {
    const auto found = gVehicleSoundProxies.find(
        GetVehicleProxyKey(owner, soundType)
    );
    return found != gVehicleSoundProxies.end() ? &found->second : nullptr;
}

bool IsVehicleEngineRendererEnabled() {
    return VehicleBackendShouldReplaceOriginal() &&
           AudioConfigVehicleEnginesEnabled();
}

bool IsVehicleEffectRendererEnabled() {
    return VehicleBackendShouldReplaceOriginal() &&
           AudioConfigVehicleEffectsEnabled();
}

template<typename T>
T ReadProxyField(
    const VehicleSoundProxy& proxy,
    std::size_t offset
) {
    T value{};
    std::memcpy(&value, proxy.sound.data() + offset, sizeof(value));
    return value;
}

void PublishVehicleStop(VehicleSoundProxy& proxy) {
    AudioJob job{};
    job.type = AudioJobType::VehicleStop;
    job.sourceKey = reinterpret_cast<std::uintptr_t>(
        proxy.sound.data()
    );
    VehicleBackendEnqueue(job);
    proxy.active = false;
}

void PublishVehicleUpdate(VehicleSoundProxy& proxy) {
    auto* entity = *reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(proxy.owner) + 4
    );
    if (!entity) {
        PublishVehicleStop(proxy);
        return;
    }

    AudioJob job{};
    job.type = AudioJobType::VehicleUpdate;
    job.sourceKey = reinterpret_cast<std::uintptr_t>(
        proxy.sound.data()
    );
    job.sourceGeneration = proxy.generation;
    job.bankId = proxy.bankId;
    job.drySoundId = ReadProxyField<std::int16_t>(
        proxy,
        kAeSoundIdOffset
    );
    job.defaultVolumeDb = ReadProxyField<float>(
        proxy,
        kAeSoundVolumeOffset
    );
    job.baseRollOffFactor = ReadProxyField<float>(
        proxy,
        kAeSoundRollOffOffset
    );
    job.baseSpeed = ReadProxyField<float>(
        proxy,
        kAeSoundSpeedOffset
    );
    const auto flags = ReadProxyField<std::uint16_t>(
        proxy,
        kAeSoundFlagsOffset
    );
    job.startPercentage = (flags & kSoundStartPercentage) != 0;
    job.keepAliveWhenSilent =
        proxy.soundType == 0x26 || proxy.soundType == 0x27;
    job.playTime = ReadProxyField<std::int16_t>(
        proxy,
        kAeSoundPlayTimeOffset
    );
    job.worldPosition = ReadPlaceablePosition(entity);
    job.relativePosition = PositionRelativeToCamera(job.worldPosition);
    const auto positionTime =
        *reinterpret_cast<const std::uint32_t*>(kGameTimeMsAddress);
    const auto currentDistance = VectorMagnitude(job.relativePosition);
    const auto dopplerScale = ReadProxyField<float>(
        proxy,
        kAeSoundDopplerOffset
    );
    job.dopplerScale =
        proxy.previousPositionTimeMs != 0
            ? reinterpret_cast<GetDopplerRelativeFrequencyFn>(
                  kGetDopplerRelativeFrequencyAddress
              )(
                  proxy.previousDistance,
                  currentDistance,
                  proxy.previousPositionTimeMs,
                  positionTime,
                  dopplerScale
              )
            : 1.0f;
    proxy.previousDistance = currentDistance;
    proxy.previousPositionTimeMs = positionTime;
    job.effectsGainDb = ReadEffectsGainDb();
    VehicleBackendEnqueue(job);
}

template<typename T>
T ReadSoundField(const std::uint8_t* sound, std::size_t offset) {
    T value{};
    std::memcpy(&value, sound + offset, sizeof(value));
    return value;
}

