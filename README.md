# SA Audio Runtime

Worker-thread weapon-audio rendering for **GTA San Andreas** and SA:MP.

SA Audio Runtime moves gunshot and bullet-impact playback away from GTA's main
audio thread while preserving the game's original sample selection and playback
behavior. This reduces frame-time spikes during crowded firefights without
calling Miles Sound System from a background thread.

Version 1.1.0 replaces weapon shots and their surface impacts. All other game
audio remains owned by GTA's original engine.

## Features

- Dedicated DirectSound worker for gunshots and bullet impacts.
- Original GENRL samples, layers, pitch variation and timing.
- Original material-dependent impact selection for pedestrians, water, wood,
  metal, concrete, gravel, tile and fallback surfaces.
- Camera-relative positioning, stereo placement and distance attenuation.
- Impact-voice prioritization under sustained automatic fire.
- Indoor tails and response to GTA's effects-volume setting.
- Automatic fallback to GTA's original renderer when the runtime is disabled
  or unavailable.
- Optional ModLoader bridge for individual weapon and bullet-impact WAV
  replacements.
- Persistent `Alt+Y` runtime toggle with on-screen status messages.
- GTA SA 1.0 US Compact and Hoodlum executable validation before hooks are
  installed.

## Requirements

- GTA San Andreas 1.0 US Compact or Hoodlum.
- An ASI loader.
- ModLoader only when WAV replacement support is needed.

SA:MP is supported. Other executable versions are rejected before game hooks
are installed.

## Installation

Extract the release archive into the GTA San Andreas directory. Its layout
already places the optional ModLoader bridge in the correct directory:

```text
AudioRuntime.asi
AudioRuntime.ini
README.txt
modloader\
  .data\
    plugins\
      AudioRuntime.ModLoader.dll
```

The ASI loader can load `AudioRuntime.asi` from the game directory. If your
setup uses a separate ASI directory, move `AudioRuntime.asi` and
`AudioRuntime.ini` there together. If the INI is missing, the plugin creates it
next to the ASI with default values.

The bridge receives ModLoader's resolved weapon-bank
`GENRL\Bank_137\sound_XXX.wav` and bullet-impact-bank
`GENRL\Bank_21\sound_XXX.wav` files through its plugin API. It follows mod
priority and live install-state changes. Individual mono PCM WAV replacements
are supported. Replacing a complete GENRL archive is not supported in 1.1.0.

## Configuration

```ini
# SA Audio Runtime v1.1.0
# Created by sonochiwa
# Source code: https://github.com/sonochiwa/sa-audio-runtime
# Default toggle hotkey: Alt + Y

[General]
IsEnabled=1
HotkeyEnabled=1
HotkeyModifier=18
HotkeyKey=89
ShowNotifications=1

[Modules]
gunshots=1
```

| Setting | Default | Meaning |
| --- | --- | --- |
| `IsEnabled` | `1` | Master state for runtime renderers. |
| `HotkeyEnabled` | `1` | Enables runtime hotkey polling. |
| `HotkeyModifier` | `18` | Modifier virtual-key code; `18` is Alt and `0` disables the modifier. |
| `HotkeyKey` | `89` | Main virtual-key code; `89` is Y. |
| `ShowNotifications` | `1` | Shows enabled/disabled messages. |
| `gunshots` | `1` | Uses the worker renderer for shots and bullet impacts. |

Press `Alt+Y` to reload the INI and switch the runtime between enabled and
disabled. The new state is written to `IsEnabled` and restored on the next
launch.

When `HotkeyEnabled=0`, no keys are polled. Configuration changes then require
a game restart. Setting `gunshots=0` returns weapon-shot and bullet-impact
ownership to GTA's original audio engine.

## Building

Open `AudioRuntime.sln` in Visual Studio 2022 and build `Release|Win32`, or run:

```bat
msbuild AudioRuntime.sln /p:Configuration=Release /p:Platform=Win32
```

Build outputs:

```text
build\AudioRuntime.asi
build\AudioRuntime.ini
build\AudioRuntime.ModLoader.dll
```

## Repository Layout

```text
Config\AudioRuntime.ini               Default configuration
src\audio_config.cpp                  Persistent settings
src\main.cpp                          GTA hooks and job capture
src\weapon_backend.cpp                Worker-thread weapon-audio renderer
src\sound_bank.cpp                    GENRL sound-bank reader
src\modloader_bridge.cpp              Optional ModLoader bridge
vendor\minhook\                       Vendored MinHook sources
AudioRuntime.sln                      Visual Studio solution
ROADMAP.md                            Planned audio families
```

## How It Works

GTA still produces the original events and playback parameters. The game thread
selects the same material-specific impact variants, captures a small job
description and places it in a lock-free queue. A dedicated worker loads the PCM
data and renders the original voices through DirectSound. The main thread only
publishes camera, effects-volume and indoor/outdoor state.

No Miles Sound System calls are made from the worker. This isolates the
background renderer from the thread-safety limitations of GTA's original audio
middleware.

See [ROADMAP.md](ROADMAP.md) for possible post-1.0 audio modules.

## License

SA Audio Runtime is released under the [MIT License](LICENSE). MinHook is
included under its own BSD-style license in
[`vendor/minhook/LICENSE.txt`](vendor/minhook/LICENSE.txt).
