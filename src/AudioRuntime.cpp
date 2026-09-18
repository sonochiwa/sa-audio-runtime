// GTA San Andreas mixes its weapon, vehicle, dialogue and world sounds on
// the game thread through the stock sound bank path, which is where the
// hitching and the clipped or missing samples come from on today's
// hardware. Audio Runtime hooks the sound requests, classifies them, and
// hands the ones it owns to a backend thread that plays the same samples
// through DirectSound with its own voices, loops, fades and listener
// transform, while everything it does not own keeps going through the game.
// The ModLoader companion feeds WAV replacements and pack sources in through
// the exported callbacks below.

#include "modules/modules.h"
#include "weapon_backend.h"

using namespace runtime;

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
