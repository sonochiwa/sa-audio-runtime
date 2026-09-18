#pragma once

#include "modules/prelude.h"

namespace runtime {

void ShowBackendState(bool enabled);
bool IsVirtualKeyDown(int key);
void ResetRuntimeSources();
void __fastcall HookAudioEngineReset(void* self, void*);
void ServiceToggleHotkey();
void __fastcall HookAudioEngineService(void* self, void*);
bool IsWeaponRendererEnabled();
bool InstallRequestNewSoundHotpatch();
void UninstallRequestNewSoundHotpatch();
void ApplyRuntimeState();
bool InstallHooks();
DWORD WINAPI WorkerThread(void*);

} // namespace runtime
