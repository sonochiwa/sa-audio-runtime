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
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>

namespace {

#include "modules/runtime_context.inl"

#include "modules/game_audio_common.inl"
#include "modules/weapon_runtime.inl"
#include "modules/vehicle_capture.inl"
#include "modules/stateful_runtime.inl"
#include "modules/vehicle_runtime.inl"
#include "modules/runtime_bootstrap.inl"

} // namespace

extern "C" __declspec(dllexport) void __cdecl
AudioRuntimeModLoaderSample(
    std::int32_t bankId,
    std::int32_t soundId,
    const char* path,
    std::int32_t installed
) {
    const auto bank = static_cast<RuntimeSoundBank>(bankId);
    if (installed) {
        WeaponBackendSetSampleOverride(
            bank,
            static_cast<std::int16_t>(soundId),
            path
        );
    } else {
        WeaponBackendClearSampleOverride(
            bank,
            static_cast<std::int16_t>(soundId)
        );
    }
}

extern "C" __declspec(dllexport) void __cdecl
AudioRuntimeModLoaderSources(
    const char* archivePath,
    const char* lookupPath
) {
    WeaponBackendSetArchiveOverride(archivePath, lookupPath);
}

extern "C" __declspec(dllexport) void __cdecl
AudioRuntimeModLoaderDynamicSample(
    std::int32_t bankId,
    std::int32_t soundId,
    const char* path,
    std::int32_t installed
) {
    WeaponBackendSetDynamicSampleOverride(
        static_cast<std::int16_t>(bankId),
        static_cast<std::int16_t>(soundId),
        path,
        installed != 0
    );
}

extern "C" __declspec(dllexport) void __cdecl
AudioRuntimeModLoaderPack(
    std::int32_t packId,
    const char* path,
    std::int32_t installed
) {
    WeaponBackendSetPackOverride(
        packId,
        path,
        installed != 0
    );
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
