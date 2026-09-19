# Audio Runtime

`AudioRuntime.asi` is a standalone GTA San Andreas plugin that renders the
game's sound effects on a worker thread instead of the main audio thread.

The plugin moves supported audio families away from GTA's main audio thread
while preserving the game's original sample selection and playback behavior,
which removes the frame-time spikes dense audio causes without calling Miles
Sound System from a background thread. It covers gunshots, bullet impacts,
vehicles, explosions, weapon effects, collisions, character and world sounds,
dialogue, the police scanner and safe one-shot paths; unsupported or disabled
sounds stay owned by GTA's original engine.

An optional ModLoader companion, `AudioRuntime.ModLoader.dll`, feeds WAV and
SFX-pack replacements from ModLoader into the runtime.

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
- Persistent Alt+Y runtime toggle with on-screen status messages.
- GTA SA 1.0 US Compact and Hoodlum executable validation before hooks are
  installed.

## Requirements

- GTA San Andreas 1.0 US (Compact or Hoodlum executable), or a SA-MP
  installation based on it.
- An ASI loader, such as Silent's ASI Loader or Ultimate ASI Loader.
- ModLoader, only for WAV and SFX-pack replacement through the companion.

Other executable versions are rejected before any game hook is installed.

## Installation

1. Extract the archive into the GTA San Andreas directory. Its layout
   already places the ModLoader companion where ModLoader loads plugins:

   ```text
   AudioRuntime.asi
   AudioRuntime.ini
   modloader\
     .data\
       plugins\
         AudioRuntime.ModLoader.dll
   ```

2. Start the game.

If your setup uses a separate ASI directory, move `AudioRuntime.asi` and
`AudioRuntime.ini` there together; the INI is created next to the plugin when
it is missing.

The companion receives ModLoader's resolved SFX paths through its plugin API
and translates pack-local `bank_N` folders to GTA's global sound-bank IDs. It
follows mod priority and live install-state changes without rescanning the
ModLoader directory. Individual mono PCM WAV files, complete
`FEET`/`GENRL`/`PAIN_A`/`SCRIPT`/`SPC_*` archives and `BankLkup.dat`
replacements are supported.

## Configuration

```ini
# Audio Runtime v2.3.0
# Created by sonochiwa
# Source code: https://github.com/sonochiwa/sa-audio-runtime
# Default toggle hotkey: Alt + Y

[general]
isEnabled=1
hotkeyEnabled=1
hotkeyModifier=18
hotkeyKey=89
```

| Setting | Default | Meaning |
| --- | ---: | --- |
| `isEnabled` | `1` | Runtime state. Written back when the hotkey toggles it. |
| `hotkeyEnabled` | `1` | Polls the toggle hotkey. |
| `hotkeyModifier` | `18` | Modifier virtual-key code; `18` is Alt and `0` disables the modifier. |
| `hotkeyKey` | `89` | Main virtual-key code; `89` is Y. |

Press Alt+Y to switch the runtime between enabled and disabled. The new state
is shown on screen, written to `isEnabled` and restored on the next launch.
When `hotkeyEnabled=0`, or when `hotkeyKey` is missing or `0`, no key is
polled and `isEnabled` is read once at startup.

## Building

Visual Studio 2022 (v143), `Release|Win32`. Open `AudioRuntime.sln` or run:

```powershell
msbuild AudioRuntime.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

The solution builds `build\AudioRuntime.asi` next to a copy of the INI and
`build\AudioRuntime.ModLoader.dll`. `Config\AudioRuntime.ini` is compiled
into the plugin as an `RCDATA` resource, so the INI written when the file is
missing is byte for byte the canonical one.

## Repository Layout

```text
AudioRuntime.sln
README.md
CHANGELOG.md
LICENSE
.github\workflows\release.yml   Tagged release build, checksum and attestation
Config\
  AudioRuntime.ini              Canonical configuration, embedded as RCDATA
src\
  AudioRuntime.cpp              DllMain and the exports the companion calls
  AudioRuntime.rc               Version resource and the embedded INI
  AudioRuntime.vcxproj
  AudioRuntime.ModLoader.cpp    ModLoader plugin entry points
  AudioRuntime.ModLoader.rc     Companion version resource
  AudioRuntime.ModLoader.vcxproj
  audio_config.cpp / audio_config.h   INI creation, reading and write-back
  sound_bank.cpp / sound_bank.h       GENRL sound-bank reader
  weapon_backend.cpp / weapon_backend.h   The backend's public interface
  resource.h
  version.h
  backend\                      Worker-thread DirectSound renderer
    backend.h                             Includes every backend header
    buffers.cpp                           DirectSound buffers and override application
    core.cpp / core.h                     Override tables, job queue, completions
    device.cpp                            Listener and device creation
    dialogue.cpp                          Dialogue jobs
    guns.cpp                              Gunshot layers, minigun, bullet hits
    listener.cpp                          Camera transform and attenuation
    prelude.h                             System includes
    thread.cpp                            The backend thread
    vehicle_banks.cpp                     Vehicle and dialogue bank loading
    vehicle_jobs.cpp                      Vehicle loop continuation and one-shots
    vehicle_sources.cpp                   Vehicle source updates
    vehicle_voices.cpp                    Vehicle voice creation and stop fades
    virtual_sources.cpp                   Virtualised runtime sources
    voice_lifecycle.cpp                   Starting, suspending, resuming and stopping voices
    voices.cpp                            Voice slots, priorities, playback, mixing
  bridge\                       ModLoader companion
    bridge.h                              Includes every bridge header
    core.cpp / core.h                     Backend lookup and state
    delivery.cpp                          Delivery to the runtime and plugin callbacks
    modloader_api.h                       The subset of the ModLoader plugin API used
    paths.cpp                             Path and bank resolution
    prelude.h                             System includes
  modules\                      Game hooks and sound capture
    game_audio_common.cpp / game_audio_common.hCamera, mixer and shared job helpers
    hooks_install.cpp                     Hook installation
    modules.h                             Includes every runtime header
    prelude.h                             System includes
    runtime_bootstrap.cpp / runtime_bootstrap.hHotkey, configuration and startup
    runtime_context.cpp / runtime_context.hGTA addresses, types and shared state
    stateful_hooks.cpp                    Sound request and cancellation hooks
    stateful_queries.cpp                  Stateful sound lookups
    stateful_runtime.cpp / stateful_runtime.hDialogue proxies
    stateful_sounds.cpp                   Stateful sound classification and service
    vehicle_capture.cpp / vehicle_capture.hVehicle source capture and job creation
    vehicle_reconcile.cpp                 Persistent vehicle sound reconciliation
    vehicle_runtime.cpp / vehicle_runtime.hVehicle ownership and lifecycle
    weapon_runtime.cpp / weapon_runtime.h Gunshot and bullet-impact capture
vendor\
  minhook\                      MinHook, compiled into the plugin
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

## Release Integrity

Tagged releases are built by GitHub Actions from the tagged commit. Each
release carries `AudioRuntime-vX.Y.Z.zip`, its SHA-256 in
`AudioRuntime-vX.Y.Z.zip.sha256` and a signed build-provenance attestation,
which proves that the archive was produced by this repository's workflow
from that revision. It does not prove the code is bug-free.

```text
gh attestation verify AudioRuntime-vX.Y.Z.zip -R sonochiwa/sa-audio-runtime
```

## License

MIT. See [LICENSE](LICENSE).
