#include "modules/modules.h"

namespace runtime {

bool InstallHooks() {
    const auto signature =
        *reinterpret_cast<const volatile std::uint32_t*>(kGameSignatureAddress);
    if (signature != kGame10UsCompactSignature &&
        signature != kGame10UsHoodlumSignature) {
        return false;
    }

    const auto Matches = [](
        std::uintptr_t address,
        const auto& prologue
    ) {
        if (std::memcmp(
                reinterpret_cast<const void*>(address),
                prologue,
                sizeof(prologue)
            ) == 0) {
            return true;
        }
        return false;
    };
    if (!Matches(
            kPlayGunSoundsAddress,
            kPlayGunSoundsPrologue
        ) ||
        !Matches(
            kPlayMinigunFireSoundsAddress,
            kPlayMinigunFireSoundsPrologue
        ) ||
        !Matches(
            kPlayBulletHitSoundAddress,
            kPlayBulletHitSoundPrologue
        ) ||
        !Matches(
            kVehicleAudioServiceAddress,
            kVehicleAudioServicePrologue
        ) ||
        !Matches(
            kVehicleAudioTerminateAddress,
            kVehicleAudioTerminatePrologue
        ) ||
        !Matches(
            kRequestPlayerEngineSoundAddress,
            kRequestPlayerEngineSoundPrologue
        ) ||
        !Matches(
            kStartDummyEngineSoundAddress,
            kStartDummyEngineSoundPrologue
        ) ||
        !Matches(
            kRequestNewSoundAddress,
            kRequestNewSoundPrologue
        ) ||
        !Matches(
            kAreBankSoundsPlayingAddress,
            kAreBankSoundsPlayingPrologue
        ) ||
        !Matches(
            kAreEventSoundsPlayingAddress,
            kAreEventSoundsPlayingPrologue
        ) ||
        !Matches(
            kAreEventPhysicalSoundsPlayingAddress,
            kAreEventPhysicalSoundsPlayingPrologue
        ) ||
        !Matches(
            kCancelEventSoundsAddress,
            kCancelEventSoundsPrologue
        ) ||
        !Matches(
            kCancelEventPhysicalSoundsAddress,
            kCancelEventPhysicalSoundsPrologue
        ) ||
        !Matches(
            kCancelBankSlotSoundsAddress,
            kCancelBankSlotSoundsPrologue
        ) ||
        !Matches(
            kCancelOwnedSoundsAddress,
            kCancelOwnedSoundsPrologue
        ) ||
        !Matches(
            kAudioEngineServiceAddress,
            kAudioEngineServicePrologue
        ) ||
        !Matches(
            kPedSpeechTerminateAddress,
            kPedSpeechTerminatePrologue
        ) ||
        !Matches(
            kPedlessSpeechTerminateAddress,
            kPedlessSpeechTerminatePrologue
        ) ||
        !Matches(
            kPoliceScannerDestructorAddress,
            kPoliceScannerDestructorPrologue
        )) {
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        return false;
    }
    struct InstallGuard {
        bool committed{};

        ~InstallGuard() {
            if (committed) {
                return;
            }
            UninstallRequestNewSoundHotpatch();
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
        }
    } installGuard;
    gOriginalCancelVehicleEngineSound =
        reinterpret_cast<VehicleCancelSoundFn>(
            kCancelVehicleEngineSoundAddress
        );

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

    if (MH_CreateHook(
            reinterpret_cast<void*>(kAudioEngineResetAddress),
            &HookAudioEngineReset,
            reinterpret_cast<void**>(&gOriginalAudioEngineReset)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kPlayMinigunFireSoundsAddress),
            &HookPlayMinigunFireSounds,
            reinterpret_cast<void**>(&gOriginalPlayMinigunFireSounds)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kPlayBulletHitSoundAddress),
            &HookPlayBulletHitSound,
            reinterpret_cast<void**>(&gOriginalPlayBulletHitSound)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kVehicleAudioServiceAddress),
            &HookVehicleAudioService,
            reinterpret_cast<void**>(&gOriginalVehicleAudioService)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kVehicleAudioTerminateAddress),
            &HookVehicleAudioTerminate,
            reinterpret_cast<void**>(&gOriginalVehicleAudioTerminate)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kRequestPlayerEngineSoundAddress),
            &HookRequestPlayerEngineSound,
            reinterpret_cast<void**>(&gOriginalRequestPlayerEngineSound)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kStartDummyEngineSoundAddress),
            &HookStartDummyEngineSound,
            reinterpret_cast<void**>(&gOriginalStartDummyEngineSound)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kAreBankSoundsPlayingAddress),
            &HookAreBankSoundsPlaying,
            reinterpret_cast<void**>(&gOriginalAreBankSoundsPlaying)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kAreEventSoundsPlayingAddress),
            &HookAreEventSoundsPlaying,
            reinterpret_cast<void**>(&gOriginalAreEventSoundsPlaying)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(
                kAreEventPhysicalSoundsPlayingAddress
            ),
            &HookAreEventPhysicalSoundsPlaying,
            reinterpret_cast<void**>(
                &gOriginalAreEventPhysicalSoundsPlaying
            )
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kCancelEventSoundsAddress),
            &HookCancelEventSounds,
            reinterpret_cast<void**>(&gOriginalCancelEventSounds)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(
                kCancelEventPhysicalSoundsAddress
            ),
            &HookCancelEventPhysicalSounds,
            reinterpret_cast<void**>(
                &gOriginalCancelEventPhysicalSounds
            )
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kCancelBankSlotSoundsAddress),
            &HookCancelBankSlotSounds,
            reinterpret_cast<void**>(&gOriginalCancelBankSlotSounds)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kCancelOwnedSoundsAddress),
            &HookCancelOwnedSounds,
            reinterpret_cast<void**>(&gOriginalCancelOwnedSounds)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kPedSpeechTerminateAddress),
            &HookPedSpeechTerminate,
            reinterpret_cast<void**>(&gOriginalPedSpeechTerminate)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kPedlessSpeechTerminateAddress),
            &HookPedlessSpeechTerminate,
            reinterpret_cast<void**>(&gOriginalPedlessSpeechTerminate)
        ) != MH_OK) {
        return false;
    }

    if (MH_CreateHook(
            reinterpret_cast<void*>(kPoliceScannerDestructorAddress),
            &HookPoliceScannerDestructor,
            reinterpret_cast<void**>(&gOriginalPoliceScannerDestructor)
        ) != MH_OK) {
        return false;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        return false;
    }

    if (!InstallRequestNewSoundHotpatch()) {
        return false;
    }

    installGuard.committed = true;
    return true;
}

DWORD WINAPI WorkerThread(void*) {
    AudioConfigInitialize(gModule);
    ApplyRuntimeState();
    if (!WeaponBackendStart(gModule)) {
        return 1;
    }
    if (!InstallHooks()) {
        WeaponBackendStop();
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

} // namespace runtime
