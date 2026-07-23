AudioJob BuildGunAudioJob(
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
) {
    AudioJob job{};
    job.drySoundId = emptySfxId;
    job.subSoundId = farSfxId;
    job.mainLeftSoundId = highPitchSfxId;
    job.mainRightSoundId = lowPitchSfxId;
    job.tailSoundId = echoSfxId;
    job.volumeOffsetDb = volumeChange;

    auto* eventVolumes = *reinterpret_cast<std::int8_t* const volatile*>(
        kEventVolumesPointerAddress
    );
    if (eventVolumes) {
        if (audioEventId >= 0 && audioEventId < 45401) {
            job.defaultVolumeDb =
                static_cast<float>(eventVolumes[audioEventId]);
        }
        job.minigunStopVolumeDb =
            static_cast<float>(eventVolumes[kWeaponFireMinigunStopEvent]);
    }

    reinterpret_cast<GetPositionRelativeToCameraFn>(
        kGetPositionRelativeToCameraAddress
    )(&job.relativePosition, entity);
    job.worldPosition = ReadPlaceablePosition(entity);

    job.baseRollOffFactor = 1.0f;
    job.baseSpeed = speed1;
    job.isAircraftWeapon =
        audioEventId == kWeaponFirePlaneEvent ||
        audioEventId == kWeaponFireMinigunPlaneEvent;
    if (audioEventId == kWeaponFirePlaneEvent) {
        auto* planeFrequencyIndex =
            static_cast<std::uint8_t*>(self) + 0x7E;
        *planeFrequencyIndex =
            static_cast<std::uint8_t>((*planeFrequencyIndex + 1) % 2);
        constexpr float variations[] = {1.08f, 1.0f};
        job.baseRollOffFactor = 1.6f;
        job.baseSpeed =
            variations[*planeFrequencyIndex] * speed1 * 0.7937f;
    } else if (audioEventId == kWeaponFireMinigunPlaneEvent) {
        job.baseRollOffFactor = 1.8f;
        job.baseSpeed = speed1 * 0.7937f;
    }

    const auto randomPitch = reinterpret_cast<RandomFloatFn>(
        kRandomFloatAddress
    )(-0.02f, 0.02f);
    job.mainSpeed = (randomPitch + 1.0f) * job.baseSpeed;
    job.tailLeftSpeed = speed2;
    job.tailRightSpeed = speed2 * 1.1892101f;
    if (reinterpret_cast<ResolveProbabilityFn>(
            kResolveProbabilityAddress
        )(0.5f)) {
        std::swap(job.tailLeftSpeed, job.tailRightSpeed);
    }
    job.effectsGainDb = ReadEffectsGainDb();
    return job;
}

void __fastcall HookPlayGunSounds(
    void* self,
    void*,
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
) {
    const bool isMinigun =
        emptySfxId == kMinigunDrySoundId &&
        farSfxId == kMinigunSubSoundId &&
        highPitchSfxId == kMinigunMainLeftSoundId &&
        lowPitchSfxId == kMinigunMainRightSoundId &&
        echoSfxId == kMinigunTailSoundId;
    constexpr std::size_t kLastGunFireTimeOffset = 0x98;
    auto* lastFireTime = reinterpret_cast<std::uint32_t*>(
        static_cast<std::uint8_t*>(self) + kLastGunFireTimeOffset
    );
    const auto lastFireTimeBefore = *lastFireTime;

    const bool replaceOriginal =
        WeaponBackendShouldReplaceOriginal() &&
        AudioConfigGunshotsEnabled();
    if (replaceOriginal) {
        const auto gameTimeMs =
            *reinterpret_cast<const volatile std::uint32_t*>(kGameTimeMsAddress);
        if (gameTimeMs < lastFireTimeBefore + 25) {
            return;
        }

        *lastFireTime = gameTimeMs;
        auto job = BuildGunAudioJob(
            self,
            entity,
            emptySfxId,
            farSfxId,
            highPitchSfxId,
            lowPitchSfxId,
            echoSfxId,
            audioEventId,
            volumeChange,
            speed1,
            speed2
        );
        if (isMinigun) {
            job.minigunMode = MinigunAudioMode::Fire;
            job.sourceKey = reinterpret_cast<std::uintptr_t>(self);
        }

        if (!WeaponBackendEnqueue(job)) {
            *lastFireTime = lastFireTimeBefore;
            gOriginalPlayGunSounds(
                self,
                entity,
                emptySfxId,
                farSfxId,
                highPitchSfxId,
                lowPitchSfxId,
                echoSfxId,
                audioEventId,
                volumeChange,
                speed1,
                speed2
            );
        }
    } else {
        gOriginalPlayGunSounds(
            self,
            entity,
            emptySfxId,
            farSfxId,
            highPitchSfxId,
            lowPitchSfxId,
            echoSfxId,
            audioEventId,
            volumeChange,
            speed1,
            speed2
        );
    }
}

void __fastcall HookPlayMinigunFireSounds(
    void* self,
    void*,
    void* entity,
    std::int32_t audioEventId
) {
    if (!WeaponBackendShouldReplaceOriginal() ||
        !AudioConfigGunshotsEnabled()) {
        gOriginalPlayMinigunFireSounds(self, entity, audioEventId);
        return;
    }

    MinigunAudioMode mode{};
    std::int32_t normalizedEvent{};
    switch (audioEventId) {
    case kWeaponFireEvent:
    case kWeaponFireMinigunAmmoEvent:
        mode = MinigunAudioMode::Fire;
        normalizedEvent = kWeaponFireMinigunAmmoEvent;
        break;
    case kWeaponFirePlaneEvent:
        mode = MinigunAudioMode::Fire;
        normalizedEvent = kWeaponFireMinigunPlaneEvent;
        break;
    case kWeaponFireMinigunNoAmmoEvent:
        mode = MinigunAudioMode::Spin;
        normalizedEvent = kWeaponFireMinigunNoAmmoEvent;
        break;
    default:
        gOriginalPlayMinigunFireSounds(self, entity, audioEventId);
        return;
    }

    constexpr std::size_t kMinigunStateOffset = 0x7F;
    constexpr std::size_t kLastMinigunUpdateTimeOffset = 0x90;
    constexpr std::uint32_t kMinigunUpdateIntervalMs = 100;
    auto* bytes = static_cast<std::uint8_t*>(self);
    auto* state = bytes + kMinigunStateOffset;
    auto* lastUpdateTime = reinterpret_cast<std::uint32_t*>(
        bytes + kLastMinigunUpdateTimeOffset
    );
    const auto desiredState = static_cast<std::uint8_t>(
        mode == MinigunAudioMode::Fire ? 1 : 0
    );
    const auto gameTimeMs =
        *reinterpret_cast<const volatile std::uint32_t*>(kGameTimeMsAddress);
    if (*state == desiredState &&
        gameTimeMs < *lastUpdateTime + kMinigunUpdateIntervalMs) {
        return;
    }
    const auto previousState = *state;
    const auto previousUpdateTime = *lastUpdateTime;
    *state = desiredState;
    *lastUpdateTime = gameTimeMs;

    const bool firing = mode == MinigunAudioMode::Fire;
    auto job = BuildGunAudioJob(
        self,
        entity,
        firing ? kMinigunDrySoundId : -1,
        firing ? kMinigunSubSoundId : -1,
        firing ? kMinigunMainLeftSoundId : -1,
        firing ? kMinigunMainRightSoundId : -1,
        firing ? kMinigunTailSoundId : -1,
        normalizedEvent,
        0.0f,
        1.0f,
        1.0f
    );
    job.minigunMode = mode;
    job.sourceKey = reinterpret_cast<std::uintptr_t>(self);
    if (!WeaponBackendEnqueue(job)) {
        *state = previousState;
        *lastUpdateTime = previousUpdateTime;
        gOriginalPlayMinigunFireSounds(self, entity, audioEventId);
    }
}

bool HasSurfaceFlag(std::uintptr_t address, std::int32_t surface) {
    return reinterpret_cast<SurfaceFlagFn>(address)(
        reinterpret_cast<void*>(kSurfaceInfosAddress),
        surface
    );
}

void __fastcall HookPlayBulletHitSound(
    void* self,
    void*,
    std::int32_t surface,
    const AudioVector* position,
    float angle
) {
    if (!WeaponBackendShouldReplaceOriginal() ||
        !AudioConfigBulletImpactsEnabled() ||
        !position ||
        surface < 0 ||
        surface >= kCollisionSurfaceCount) {
        gOriginalPlayBulletHitSound(self, surface, position, angle);
        return;
    }

    std::int32_t minimumSoundId{};
    std::int32_t maximumSoundId{};
    float volumeOffsetDb{};
    float rollOffFactor = 1.5f;
    if (surface == kPedSurface) {
        minimumSoundId = 7;
        maximumSoundId = 9;
    } else if (HasSurfaceFlag(kIsAudioWaterAddress, surface)) {
        minimumSoundId = 16;
        maximumSoundId = 18;
        volumeOffsetDb = 6.0f;
        rollOffFactor = 2.0f;
    } else if (HasSurfaceFlag(kIsAudioWoodAddress, surface)) {
        minimumSoundId = 19;
        maximumSoundId = 21;
    } else if (HasSurfaceFlag(kIsAudioMetalAddress, surface)) {
        if (reinterpret_cast<ResolveProbabilityFn>(
                kResolveProbabilityAddress
            )((90.0f - angle) / 180.0f)) {
            minimumSoundId = 10;
            maximumSoundId = 12;
        } else {
            minimumSoundId = 4;
            maximumSoundId = 6;
        }
    } else if (
        HasSurfaceFlag(kIsAudioConcreteAddress, surface) ||
        HasSurfaceFlag(kIsAudioGravelAddress, surface) ||
        HasSurfaceFlag(kIsAudioTileAddress, surface)
    ) {
        minimumSoundId = 13;
        maximumSoundId = 15;
    } else {
        minimumSoundId = 1;
        maximumSoundId = 3;
    }

    constexpr std::size_t kLastBulletHitSoundOffset = 0x202;
    auto* lastSoundId = reinterpret_cast<std::int16_t*>(
        static_cast<std::uint8_t*>(self) + kLastBulletHitSoundOffset
    );
    std::int32_t soundId{};
    do {
        soundId = reinterpret_cast<RandomIntFn>(kRandomIntAddress)(
            minimumSoundId,
            maximumSoundId
        );
    } while (soundId == *lastSoundId);

    AudioJob job{};
    job.type = AudioJobType::BulletHit;
    job.drySoundId = static_cast<std::int16_t>(soundId);
    job.worldPosition = *position;
    job.relativePosition = PositionRelativeToCamera(*position);
    job.volumeOffsetDb = volumeOffsetDb;
    job.baseRollOffFactor = rollOffFactor;
    job.baseSpeed =
        1.0f +
        reinterpret_cast<RandomFloatFn>(kRandomFloatAddress)(
            -0.02f,
            0.02f
        );
    auto* eventVolumes = *reinterpret_cast<std::int8_t* const volatile*>(
        kEventVolumesPointerAddress
    );
    if (eventVolumes) {
        job.defaultVolumeDb =
            static_cast<float>(eventVolumes[kBulletHitEvent]);
    }
    job.effectsGainDb = ReadEffectsGainDb();

    if (WeaponBackendEnqueue(job)) {
        *lastSoundId = static_cast<std::int16_t>(soundId);
    } else {
        gOriginalPlayBulletHitSound(self, surface, position, angle);
    }
}

