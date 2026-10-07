#include "autotest/autotest.h"

#include "autotest/probe.h"
#include "autotest/scene.h"
#include "autotest/test_addresses.h"
#include "autotest/test_game.h"
#include "audio_config.h"
#include "modules/hook_freeze.h"
#include "modules/runtime_bootstrap.h"

#include "MinHook.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace autotest {
namespace {

constexpr char kVariable[] = "AUDIORUNTIME_AUTOTEST";
constexpr char kPassVariable[] = "AUDIORUNTIME_PASS";
constexpr char kSceneTest[] = "scene";
constexpr char kVanillaPass[] = "vanilla";
// Movies are skipped once they have played this long, as a player would;
// skipping on their first frame hung the game at start-up in Resource
// System's runs.
constexpr DWORD kMovieSkipDelayMs = 1000;

using GameFunction = void(__cdecl*)();

GameFunction gUpdatePads{};
GameFunction gGameProcess{};
GameFunction gScriptsProcess{};
bool gRuntimePass{true};
bool gSceneStarted{};
bool gSceneDone{};

std::string ReadVariable(const char* name) {
    char value[64]{};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return length > 0 && length < sizeof(value) ? std::string(value, length) : std::string();
}

std::string LogPath(HMODULE module) {
    char path[MAX_PATH]{};
    GetModuleFileNameA(module, path, MAX_PATH);
    if (auto* slash = std::strrchr(path, '\\')) {
        slash[1] = '\0';
    }
    return std::string(path) + "AudioRuntime.autotest.log";
}

template <typename T>
T& Global(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

bool MoviePlayedLongEnough(std::int32_t state) {
    static std::int32_t movie = -1;
    static DWORD since = 0;
    const DWORD now = GetTickCount();
    if (movie != state) {
        movie = state;
        since = now;
    }
    return now - since >= kMovieSkipDelayMs;
}

// The transitions WinMain makes when a key is pressed during a movie, and a
// closed menu in FrontendIdle, after which WinMain starts a new game.
void SkipToNewGame() {
    auto& state = Global<std::int32_t>(kGameState);
    if (state == kGameStatePlayingLogo || state == kGameStatePlayingIntro) {
        if (MoviePlayedLongEnough(state)) {
            state = state == kGameStatePlayingLogo ? kGameStateTitle : kGameStateFrontendLoading;
        }
    } else if (state == kGameStateFrontendIdle) {
        Global<std::uint8_t>(kMenuActivateNextFrame) = 0;
        Global<std::uint8_t>(kMenuActive) = 0;
    }
}

void __cdecl UpdatePadsHook() {
    static std::int32_t lastState = -1;
    const auto state = GameState();
    if (state != lastState) {
        Log("autotest: game state %d", state);
        lastState = state;
    }
    SkipToNewGame();
    gUpdatePads();
    if (gSceneStarted && !gSceneDone) {
        ApplyPad();
    }
}

void __cdecl GameProcessHook() {
    if (!gSceneStarted && Player()) {
        gSceneStarted = true;
        Log("autotest: scene starts, %s pass", gRuntimePass ? "runtime" : "vanilla");
        StartScene(gRuntimePass);
    } else if (gSceneStarted && !gSceneDone && SceneFrame()) {
        gSceneDone = true;
        Log("autotest: finished");
        RequestQuit();
    }
    gGameProcess();
}

// CGame::Initialise runs the scripts once to create the player; during the
// scene they stay halted, so no mission moves the player or the camera.
void __cdecl ScriptsProcessHook() {
    if (!gSceneStarted) {
        gScriptsProcess();
    }
}

LONG CALLBACK LogAccessViolation(EXCEPTION_POINTERS* exception) {
    const auto& record = *exception->ExceptionRecord;
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        Log("autotest: access violation at %p reading %p", record.ExceptionAddress,
            reinterpret_cast<void*>(record.ExceptionInformation[1]));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool Hook(const CodeSite& site, void* detour, GameFunction* original) {
    const auto status = MH_CreateHook(reinterpret_cast<void*>(site.address), detour,
                                      reinterpret_cast<void**>(original));
    if (status != MH_OK) {
        Log("autotest: cannot hook %s (MinHook %d)", site.name, static_cast<int>(status));
        return false;
    }
    return true;
}

bool EnableHooks() {
    runtime::HookFreezeLock freeze;
    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}

} // namespace

bool StartAutotest(HMODULE module) {
    if (ReadVariable(kVariable) != kSceneTest) {
        return false;
    }
    if (!OpenLog(LogPath(module).c_str())) {
        return false;
    }
    gRuntimePass = ReadVariable(kPassVariable) != kVanillaPass;
    Log("autotest: %s pass, game state %d", gRuntimePass ? "runtime" : "vanilla", GameState());
    if (!VerifySites()) {
        Log("autotest: not started");
        return false;
    }
    if (!gRuntimePass) {
        AudioConfigOverrideEnabled(false);
        runtime::ApplyRuntimeState();
    }
    InstallProbe(gRuntimePass);
    AddVectoredExceptionHandler(0, LogAccessViolation);
    if (!Hook(kUpdatePads, reinterpret_cast<void*>(&UpdatePadsHook), &gUpdatePads) ||
        !Hook(kGameProcess, reinterpret_cast<void*>(&GameProcessHook), &gGameProcess) ||
        !Hook(kScriptsProcess, reinterpret_cast<void*>(&ScriptsProcessHook),
              &gScriptsProcess) ||
        !EnableHooks()) {
        Log("autotest: hooks failed");
        return false;
    }
    return true;
}

} // namespace autotest
