#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <MinHook.h>

#include "audio_config.h"
#include "weapon_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr std::uintptr_t kPlayGunSoundsAddress = 0x503CE0;
constexpr std::uintptr_t kAudioEngineServiceAddress = 0x507750;
constexpr std::uintptr_t kGameSignatureAddress = 0x401000;
constexpr std::uintptr_t kGetPositionRelativeToCameraAddress = 0x4D8340;
constexpr std::uintptr_t kVectorRelativeToCameraAddress = 0x4D80B0;
constexpr std::uintptr_t kCanSeeOutsideAddress = 0x53C4A0;
constexpr std::uintptr_t kAddMessageJumpAddress = 0x69F1E0;
constexpr std::uintptr_t kRandomFloatAddress = 0x4D9C50;
constexpr std::uintptr_t kResolveProbabilityAddress = 0x4D9C80;
constexpr std::uintptr_t kEventVolumesPointerAddress = 0xBD00F8;
constexpr std::uintptr_t kAudioHardwareAddress = 0xB5F8B8;
constexpr std::size_t kEffectMasterScaleOffset = 0x414;
constexpr std::size_t kEffectsFaderScaleOffset = 0x41C;
constexpr std::size_t kNonStreamFaderScaleOffset = 0x420;
constexpr std::int32_t kWeaponFirePlaneEvent = 149;
constexpr std::int32_t kWeaponFireMinigunPlaneEvent = 152;
constexpr std::int16_t kMinigunDrySoundId = 15;
constexpr std::int16_t kMinigunSubSoundId = 16;
constexpr std::int16_t kMinigunMainLeftSoundId = 11;
constexpr std::int16_t kMinigunMainRightSoundId = 12;
constexpr std::int16_t kMinigunTailSoundId = 13;
constexpr std::uint32_t kGame10UsCompactSignature = 0x53EC8B55;
constexpr std::uint32_t kGame10UsHoodlumSignature = 0x16197BE9;
constexpr std::uint8_t kPlayGunSoundsPrologue[] = {
    0x83, 0xEC, 0x28, 0x53, 0x55, 0x56, 0x57, 0x6A, 0x05, 0x8B, 0xF1
};
constexpr std::uint8_t kAudioEngineServicePrologue[] = {
    0x56, 0x57, 0x8B, 0xF1, 0xE9
};
using PlayGunSoundsFn = void(__thiscall*)(
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
);

using ServiceFn = void(__thiscall*)(void* self);
using GetPositionRelativeToCameraFn = void(__cdecl*)(AudioVector*, void*);
using VectorRelativeToCameraFn =
    void(__cdecl*)(AudioVector*, const AudioVector*);
using RandomFloatFn = float(__cdecl*)(float, float);
using ResolveProbabilityFn = bool(__cdecl*)(float);
using CanSeeOutsideFn = bool(__cdecl*)();
using AddMessageJumpFn =
    void(__cdecl*)(const char*, std::uint32_t, std::uint16_t, bool);

HMODULE gModule{};
PlayGunSoundsFn gOriginalPlayGunSounds{};
ServiceFn gOriginalAudioEngineService{};
bool gHotkeyWasDown{};

float LinearGainToDb(float gain) {
    return gain > 0.00001f ? 20.0f * std::log10(gain) : -100.0f;
}

float ReadEffectsGainDb() {
    const auto* hardware = reinterpret_cast<const volatile std::uint8_t*>(
        kAudioHardwareAddress
    );
    const auto ReadFloat = [hardware](std::size_t offset) {
        return *reinterpret_cast<const volatile float*>(hardware + offset);
    };
    const auto gain =
        std::max(ReadFloat(kEffectMasterScaleOffset), 0.0f) *
        std::max(ReadFloat(kEffectsFaderScaleOffset), 0.0f) *
        std::max(ReadFloat(kNonStreamFaderScaleOffset), 0.0f);
    return LinearGainToDb(gain);
}

AudioVector ReadPlaceablePosition(void* entity) {
    if (!entity) {
        return {};
    }
    const auto* bytes = static_cast<const std::uint8_t*>(entity);
    const auto* matrix = *reinterpret_cast<const std::uint8_t* const*>(
        bytes + 0x14
    );
    if (matrix) {
        return *reinterpret_cast<const AudioVector*>(matrix + 0x30);
    }
    return *reinterpret_cast<const AudioVector*>(bytes + 0x4);
}

AudioVector PositionRelativeToCamera(const AudioVector& position) {
    AudioVector relative{};
    reinterpret_cast<VectorRelativeToCameraFn>(
        kVectorRelativeToCameraAddress
    )(&relative, &position);
    return relative;
}

void PublishCameraTransform() {
    const AudioVector zero{};
    const AudioVector unitX{1.0f, 0.0f, 0.0f};
    const AudioVector unitY{0.0f, 1.0f, 0.0f};
    const AudioVector unitZ{0.0f, 0.0f, 1.0f};
    const auto origin = PositionRelativeToCamera(zero);
    const auto xPoint = PositionRelativeToCamera(unitX);
    const auto yPoint = PositionRelativeToCamera(unitY);
    const auto zPoint = PositionRelativeToCamera(unitZ);
    WeaponBackendUpdateCameraTransform({
        origin,
        {
            xPoint.x - origin.x,
            xPoint.y - origin.y,
            xPoint.z - origin.z
        },
        {
            yPoint.x - origin.x,
            yPoint.y - origin.y,
            yPoint.z - origin.z
        },
        {
            zPoint.x - origin.x,
            zPoint.y - origin.y,
            zPoint.z - origin.z
        }
    });
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
    if (isMinigun) {
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
        return;
    }

    constexpr std::size_t kLastGunFireTimeOffset = 0x98;
    constexpr std::uintptr_t kGameTimeMsAddress = 0xB7CB84;
    auto* lastFireTime = reinterpret_cast<std::uint32_t*>(
        static_cast<std::uint8_t*>(self) + kLastGunFireTimeOffset
    );
    const auto lastFireTimeBefore = *lastFireTime;

    const bool replaceOriginal = WeaponBackendShouldReplaceOriginal();
    if (replaceOriginal) {
        const auto gameTimeMs =
            *reinterpret_cast<const volatile std::uint32_t*>(kGameTimeMsAddress);
        if (gameTimeMs < lastFireTimeBefore + 25) {
            return;
        }

        *lastFireTime = gameTimeMs;
        GunAudioJob job{};
        job.drySoundId = emptySfxId;
        job.subSoundId = farSfxId;
        job.mainLeftSoundId = highPitchSfxId;
        job.mainRightSoundId = lowPitchSfxId;
        job.tailSoundId = echoSfxId;
        job.volumeOffsetDb = volumeChange;

        auto* eventVolumes = *reinterpret_cast<std::int8_t* const volatile*>(
            kEventVolumesPointerAddress
        );
        if (eventVolumes && audioEventId >= 0 && audioEventId < 45401) {
            job.defaultVolumeDb = static_cast<float>(eventVolumes[audioEventId]);
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

void ShowBackendState(bool enabled) {
    if (!AudioConfigShowNotifications()) {
        return;
    }
    reinterpret_cast<AddMessageJumpFn>(kAddMessageJumpAddress)(
        enabled
            ? "~g~SA Audio Runtime: enabled"
            : "~r~SA Audio Runtime: disabled",
        2500,
        0,
        false
    );
}

bool IsVirtualKeyDown(int key) {
    return key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
}

void ServiceToggleHotkey() {
    if (!AudioConfigHotkeyEnabled()) {
        gHotkeyWasDown = false;
        return;
    }

    const int key = AudioConfigHotkeyKey();
    const int modifier = AudioConfigHotkeyModifier();
    const bool hotkeyDown =
        IsVirtualKeyDown(key) &&
        (modifier == 0 || IsVirtualKeyDown(modifier));
    if (hotkeyDown && !gHotkeyWasDown) {
        AudioConfigReload();
        const bool enabled = !AudioConfigIsEnabled();
        AudioConfigSetEnabled(enabled);
        WeaponBackendSetEnabled(
            enabled && AudioConfigGunshotsEnabled()
        );
        ShowBackendState(enabled);
    }
    gHotkeyWasDown = hotkeyDown;
}

void __fastcall HookAudioEngineService(void* self, void*) {
    gOriginalAudioEngineService(self);
    PublishCameraTransform();
    WeaponBackendUpdateEnvironment(
        reinterpret_cast<CanSeeOutsideFn>(
            kCanSeeOutsideAddress
        )()
    );
    ServiceToggleHotkey();
}

bool IsRuntimeRendererEnabled() {
    return
        AudioConfigIsEnabled() &&
        AudioConfigGunshotsEnabled();
}

void ApplyRuntimeState() {
    WeaponBackendSetEnabled(
        IsRuntimeRendererEnabled()
    );
}

bool InstallHooks() {
    const auto signature =
        *reinterpret_cast<const volatile std::uint32_t*>(kGameSignatureAddress);
    if (signature != kGame10UsCompactSignature &&
        signature != kGame10UsHoodlumSignature) {
        return false;
    }

    if (std::memcmp(
            reinterpret_cast<const void*>(kPlayGunSoundsAddress),
            kPlayGunSoundsPrologue,
            sizeof(kPlayGunSoundsPrologue)
        ) != 0 ||
        std::memcmp(
            reinterpret_cast<const void*>(kAudioEngineServiceAddress),
            kAudioEngineServicePrologue,
            sizeof(kAudioEngineServicePrologue)
        ) != 0) {
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kPlayGunSoundsAddress),
            &HookPlayGunSounds,
            reinterpret_cast<void**>(&gOriginalPlayGunSounds)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kAudioEngineServiceAddress),
            &HookAudioEngineService,
            reinterpret_cast<void**>(&gOriginalAudioEngineService)
        ) != MH_OK) {
        return false;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        return false;
    }

    return true;
}

DWORD WINAPI WorkerThread(void*) {
    AudioConfigInitialize(gModule);
    ApplyRuntimeState();
    if (!WeaponBackendStart(gModule)) {
        return 1;
    }
    if (!InstallHooks()) {
        return 2;
    }
    if (const auto bridge =
            GetModuleHandleA("AudioRuntime.ModLoader.dll")) {
        using ReplayFn = void(__cdecl*)();
        auto replay = reinterpret_cast<ReplayFn>(
            GetProcAddress(
                bridge,
                "AudioRuntimeReplayModLoaderSamples"
            )
        );
        if (!replay) {
            replay = reinterpret_cast<ReplayFn>(
                GetProcAddress(
                    bridge,
                    "_AudioRuntimeReplayModLoaderSamples"
                )
            );
        }
        if (replay) {
            replay();
        }
    }
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) void __cdecl
AudioRuntimeModLoaderSample(
    std::int32_t soundId,
    const char* path,
    std::int32_t installed
) {
    if (installed) {
        WeaponBackendSetSampleOverride(
            static_cast<std::int16_t>(soundId),
            path
        );
    } else {
        WeaponBackendClearSampleOverride(
            static_cast<std::int16_t>(soundId)
        );
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        gModule = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE thread =
                CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        WeaponBackendStop();
    }
    return TRUE;
}
