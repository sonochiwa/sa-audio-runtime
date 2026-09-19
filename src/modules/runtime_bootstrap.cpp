#include "modules/modules.h"

namespace runtime {

void ShowBackendState(bool enabled) {
    reinterpret_cast<AddMessageJumpFn>(kAddMessageJumpAddress)(
        enabled
            ? "~g~Audio Runtime: enabled"
            : "~r~Audio Runtime: disabled",
        2500,
        0,
        false
    );
}

bool IsVirtualKeyDown(int key) {
    return key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
}

void ResetRuntimeSources() {
    gVehicleSoundProxies.clear();
    gVehicleAudioOwners.clear();
    gDialogueSoundProxies.clear();
    gStatefulSoundProxies.clear();
    EndVehicleCapture();
    WeaponBackendReset();
}

void __fastcall HookAudioEngineReset(void* self, void*) {
    gOriginalAudioEngineReset(self);
    ResetRuntimeSources();
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
    return AudioConfigIsEnabled();
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
    VehicleBackendSetEnabled(AudioConfigIsEnabled());
    DialogueBackendSetEnabled(AudioConfigIsEnabled());
}

} // namespace runtime
