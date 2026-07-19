#pragma once

#include <windows.h>

void AudioConfigInitialize(HMODULE module);
void AudioConfigReload();
bool AudioConfigIsEnabled();
void AudioConfigSetEnabled(bool enabled);
bool AudioConfigHotkeyEnabled();
int AudioConfigHotkeyKey();
int AudioConfigHotkeyModifier();
bool AudioConfigShowNotifications();
bool AudioConfigGunshotsEnabled();
