#pragma once

#include <windows.h>

void AudioConfigInitialize(HMODULE module);
void AudioConfigReload();
bool AudioConfigIsEnabled();
void AudioConfigSetEnabled(bool enabled);

// Changes the state for this session only; the INI keeps its value. Used by
// the debug autotest.
void AudioConfigOverrideEnabled(bool enabled);
