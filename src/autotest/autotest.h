#pragma once

#include <windows.h>

// Debug-build check of the runtime against the game itself. With the
// environment variable AUDIORUNTIME_AUTOTEST set to "scene", the game skips
// the movies and the main menu, starts a new game, halts the mission scripts
// once the player exists, plays the scene of scene.h, writes
// AudioRuntime.autotest.log next to the plugin and quits.
// AUDIORUNTIME_PASS=vanilla plays the same scene with the runtime off for
// this session, so the two logs can be compared request by request.
namespace autotest {

constexpr bool kAutotestBuild =
#ifdef _DEBUG
    true;
#else
    false;
#endif

// True when the variable is set and the test hooks are live.
bool StartAutotest(HMODULE module);

} // namespace autotest
