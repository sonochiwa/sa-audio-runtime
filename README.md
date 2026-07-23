# SA Audio Runtime

Worker-thread game-audio rendering for **GTA San Andreas** and SA:MP.

SA Audio Runtime moves supported audio families away from GTA's main audio
thread while preserving the game's original sample selection and playback
behavior. This reduces frame-time spikes under dense audio load without calling
Miles Sound System from a background thread.

Version 2.0.0 expands the runtime from gunshots and bullet impacts to supported
vehicle, explosion, weapon-effect, collision, character, world, dialogue,
police-scanner and safe one-shot paths. Unsupported or disabled sounds remain
owned by GTA's original engine.

## Features

- Dedicated DirectSound worker for gunshots and bullet impacts.
- Worker-owned vehicle engines, drivetrain, road and tire loops, skids, horns,
  sirens and supported vehicle one-shot effects.
- Worker-owned positional ped speech and scripted dialogue, with GTA's
  original speech-slot completion callbacks preserved.
- Worker-owned police scanner speech and safe untracked one-shot effects from
  the loaded `FEET`, `GENRL`, `PAIN_A`, `SCRIPT` and `SPC_*` banks.
- Stateful worker voices for explosions, rockets, weapon mechanics, chainsaw,
  flamethrower, collisions, footsteps, jetpack, weather, fire and water.
- GTA-owned state calculations, completion callbacks, entity following,
  cancellation queries, frame delay and sample loop points.
- Original GENRL samples, layers, pitch variation and timing.
- Original material-dependent impact selection for pedestrians, water, wood,
  metal, concrete, gravel, tile and fallback surfaces.
- Camera-relative positioning, stereo placement and distance attenuation.
- Impact-voice prioritization under sustained automatic fire.
- Indoor tails and response to GTA's effects-volume setting.
- Automatic fallback to GTA's original renderer when the runtime is disabled
  or unavailable.
- Optional ModLoader bridge for individual WAV and complete SFX-pack
  replacements across `FEET`, `GENRL`, `PAIN_A`, `SCRIPT` and `SPC_*`.
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

The bridge receives ModLoader's resolved SFX paths through its plugin API and
translates pack-local `bank_N` folders to GTA's global sound-bank IDs. It
follows mod priority and live install-state changes without rescanning the
ModLoader directory. Individual mono PCM WAV files, complete
`FEET`/`GENRL`/`PAIN_A`/`SCRIPT`/`SPC_*` archives and `BankLkup.dat`
replacements are supported.

## Configuration

```ini
# SA Audio Runtime v2.0.0
# Created by sonochiwa
# Source code: https://github.com/sonochiwa/sa-audio-runtime
# Default toggle hotkey: Alt + Y

[General]
isEnabled=1
hotkeyEnabled=1
hotkeyModifier=18
hotkeyKey=89
showNotifications=1

[WeaponAudio]
gunshots=1
bulletImpacts=1
effects=1

[VehicleAudio]
engines=1
effects=1
collisions=1

[CharacterAudio]
dialogues=1
scanner=1
effects=1

[WorldAudio]
explosions=1
ambience=1
miscEffects=1
```

| Setting | Default | Meaning |
| --- | --- | --- |
| `isEnabled` | `1` | Master state for runtime renderers. |
| `hotkeyEnabled` | `1` | Enables runtime hotkey polling. |
| `hotkeyModifier` | `18` | Modifier virtual-key code; `18` is Alt and `0` disables the modifier. |
| `hotkeyKey` | `89` | Main virtual-key code; `89` is Y. |
| `showNotifications` | `1` | Shows enabled/disabled messages. |
| `WeaponAudio.gunshots` | `1` | Uses the worker renderer for weapon shots and minigun states. |
| `WeaponAudio.bulletImpacts` | `1` | Uses the worker renderer for material-dependent bullet impacts. |
| `WeaponAudio.effects` | `1` | Uses the worker renderer for reloads, mechanics and stateful weapon loops. |
| `VehicleAudio.engines` | `1` | Uses the worker renderer for player and traffic engine voices. |
| `VehicleAudio.effects` | `1` | Uses the worker renderer for road, tires, reverse, skids, horns, sirens and supported vehicle one-shots. |
| `VehicleAudio.collisions` | `1` | Uses the worker renderer for collisions, glass, water contact and doors. |
| `CharacterAudio.dialogues` | `1` | Uses the worker renderer for supported speech and scripted dialogue. |
| `CharacterAudio.scanner` | `1` | Uses the worker renderer for police-scanner dialogue. |
| `CharacterAudio.effects` | `1` | Uses the worker renderer for footsteps, movement and character effects. |
| `WorldAudio.explosions` | `1` | Uses the worker renderer for explosions and projectile layers. |
| `WorldAudio.ambience` | `1` | Uses the worker renderer for supported weather, fire, water and script ambience. |
| `WorldAudio.miscEffects` | `1` | Uses the worker renderer for safe uncategorized one-shot effects. |

Press `Alt+Y` to reload the INI and switch the runtime between enabled and
disabled. The new state is written to `isEnabled` and restored on the next
launch.

When `hotkeyEnabled=0`, no keys are polled. Configuration changes then require
a game restart. Setting a module to `0` returns that audio family to GTA's
original audio engine. Existing `[Modules]` configurations are migrated
automatically to the categorized sections when the plugin reads the INI.

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
src\main.cpp                          ASI entry points and module composition
src\modules\runtime_context.inl       GTA addresses, types and shared state
src\modules\game_audio_common.inl     Camera, mixer and shared job helpers
src\modules\weapon_runtime.inl        Gunshot and bullet-impact capture
src\modules\vehicle_capture.inl       Vehicle source capture and job creation
src\modules\stateful_runtime.inl      Dialogue and stateful sound proxies
src\modules\vehicle_runtime.inl       Vehicle ownership and lifecycle
src\modules\runtime_bootstrap.inl     Hotkey, hooks and startup
src\weapon_backend.cpp                Worker-thread audio renderer
src\sound_bank.cpp                    GENRL sound-bank reader
src\modloader_bridge.cpp              Optional ModLoader bridge
vendor\minhook\                       Vendored MinHook sources
AudioRuntime.sln                      Visual Studio solution
ROADMAP.md                            Planned audio families
```

## How It Works

GTA still produces the original events and playback parameters. The game thread
captures compact jobs while a dedicated worker loads PCM data and renders the
original voices through DirectSound. One-shot events use a bounded lock-free
queue. Frequently updated engines and other persistent sources use
latest-state mailboxes, so obsolete per-frame updates cannot accumulate behind
newer audio work.

Persistent sources outside the audible range remain alive as virtual sources
without occupying DirectSound buffers. Their playback position and GTA-facing
lifecycle continue to advance, and a physical voice is restored when the
source becomes audible. Completion notifications use a separate synchronized
queue and cannot be lost when the one-shot job queue is saturated.

Weapons, vehicles and general runtime effects share a 96-voice physical
budget. Category limits and soft reservations prevent one sound family from
occupying the complete mixer, while loudness-aware replacement keeps nearby
and frontend sounds ahead of quiet background layers.

The worker checks DirectSound health and recreates the device after buffer or
device loss. Stateful and dialogue voices are virtualized before recovery so
their GTA lifecycle and playback position survive the reset.

The main thread retains GTA's `UpdateParameters` callbacks and only publishes
camera, effects-volume and indoor/outdoor state. Hook installation is
transactional: unsupported executable bytes or a failed hook restore the
original path instead of leaving a partially active runtime.

No Miles Sound System calls are made from the worker. This isolates the
background renderer from the thread-safety limitations of GTA's original audio
middleware. Radio, music, cutscene tracks and SA:MP URL streams are outside the
project scope and remain on their existing streaming paths.

See [ROADMAP.md](ROADMAP.md) for implementation and validation status.

## License

SA Audio Runtime is released under the [MIT License](LICENSE). MinHook is
included under its own BSD-style license in
[`vendor/minhook/LICENSE.txt`](vendor/minhook/LICENSE.txt).
