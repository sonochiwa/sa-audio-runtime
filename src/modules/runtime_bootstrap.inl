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

void ApplyRuntimeState();

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
        ApplyRuntimeState();
        ShowBackendState(enabled);
    }
    gHotkeyWasDown = hotkeyDown;
}

void __fastcall HookAudioEngineService(void* self, void*) {
    gOriginalAudioEngineService(self);
    ServiceDialogueProxies();
    ServiceStatefulSounds();
    PublishCameraTransform();
    WeaponBackendUpdateEnvironment(
        reinterpret_cast<CanSeeOutsideFn>(
            kCanSeeOutsideAddress
        )()
    );
    ServiceToggleHotkey();
}

bool IsWeaponRendererEnabled() {
    return
        AudioConfigIsEnabled() &&
        (AudioConfigGunshotsEnabled() ||
         AudioConfigBulletImpactsEnabled());
}

bool InstallRequestNewSoundHotpatch() {
    if (std::memcmp(
            reinterpret_cast<const void*>(
                kRequestNewSoundHotpatchAddress
            ),
            kRequestNewSoundHotpatchPadding,
            sizeof(kRequestNewSoundHotpatchPadding)
        ) != 0) {
        return false;
    }

    auto* gateway = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        16,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE
    ));
    if (!gateway) {
        return false;
    }

    gateway[0] = 0x56;
    gateway[1] = 0x57;
    gateway[2] = 0xE9;
    const auto gatewayDisplacement = static_cast<std::int32_t>(
        (kRequestNewSoundAddress + 2) -
        reinterpret_cast<std::uintptr_t>(gateway + 7)
    );
    std::memcpy(gateway + 3, &gatewayDisplacement, sizeof(gatewayDisplacement));
    FlushInstructionCache(GetCurrentProcess(), gateway, 7);

    DWORD oldProtection{};
    auto* patch = reinterpret_cast<std::uint8_t*>(
        kRequestNewSoundHotpatchAddress
    );
    if (!VirtualProtect(
            patch,
            7,
            PAGE_EXECUTE_READWRITE,
            &oldProtection
        )) {
        VirtualFree(gateway, 0, MEM_RELEASE);
        return false;
    }

    gOriginalRequestNewSound = reinterpret_cast<RequestNewSoundFn>(gateway);
    gRequestNewSoundGateway = gateway;
    patch[0] = 0xE9;
    const auto hookDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(&HookRequestNewSound) -
        kRequestNewSoundAddress
    );
    std::memcpy(patch + 1, &hookDisplacement, sizeof(hookDisplacement));
    *reinterpret_cast<volatile std::uint16_t*>(
        kRequestNewSoundAddress
    ) = 0xF9EB;
    FlushInstructionCache(GetCurrentProcess(), patch, 7);

    DWORD ignored{};
    VirtualProtect(patch, 7, oldProtection, &ignored);
    gRequestNewSoundHotpatchInstalled = true;
    return true;
}

void UninstallRequestNewSoundHotpatch() {
    if (!gRequestNewSoundHotpatchInstalled) {
        return;
    }
    auto* patch = reinterpret_cast<std::uint8_t*>(
        kRequestNewSoundHotpatchAddress
    );
    DWORD oldProtection{};
    if (VirtualProtect(
            patch,
            7,
            PAGE_EXECUTE_READWRITE,
            &oldProtection
        )) {
        std::memcpy(
            patch,
            kRequestNewSoundHotpatchPadding,
            sizeof(kRequestNewSoundHotpatchPadding)
        );
        patch[5] = kRequestNewSoundPrologue[0];
        patch[6] = kRequestNewSoundPrologue[1];
        FlushInstructionCache(
            GetCurrentProcess(),
            patch,
            sizeof(kRequestNewSoundHotpatchPadding)
        );
        FlushInstructionCache(
            GetCurrentProcess(),
            reinterpret_cast<const void*>(kRequestNewSoundAddress),
            2
        );
        DWORD ignored{};
        VirtualProtect(patch, 7, oldProtection, &ignored);
    }
    if (gRequestNewSoundGateway) {
        VirtualFree(gRequestNewSoundGateway, 0, MEM_RELEASE);
        gRequestNewSoundGateway = nullptr;
    }
    gOriginalRequestNewSound = nullptr;
    gRequestNewSoundHotpatchInstalled = false;
}

void ApplyRuntimeState() {
    WeaponBackendSetEnabled(
        IsWeaponRendererEnabled()
    );
    VehicleBackendSetEnabled(
        AudioConfigIsEnabled() &&
        (AudioConfigVehicleEnginesEnabled() ||
         AudioConfigVehicleEffectsEnabled())
    );
    DialogueBackendSetEnabled(
        AudioConfigIsEnabled() &&
        (AudioConfigDialoguesEnabled() ||
         AudioConfigScannerEnabled() ||
         AudioConfigMiscEffectsEnabled() ||
         AudioConfigExplosionsEnabled() ||
         AudioConfigWeaponEffectsEnabled() ||
         AudioConfigVehicleCollisionsEnabled() ||
         AudioConfigCharacterEffectsEnabled() ||
         AudioConfigWorldAmbienceEnabled())
    );
}

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

