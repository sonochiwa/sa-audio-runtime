# Audio Runtime

`AudioRuntime.asi` is a GTA San Andreas plugin that renders the game's sound
effects on a worker thread instead of the main audio thread.

Dense audio, such as gunfights, traffic and explosions, costs frame time in
the stock game because every effect is processed on the main thread. The
plugin renders the supported sound families on its own DirectSound worker
with the game's original samples, selection and timing; anything unsupported
stays with the game's engine.

## Features

- Gunshots, bullet impacts and weapon effects.
- Vehicle engines, road, tire, skid, horn, siren and one-shot effects.
- Ped speech, scripted dialogue and the police scanner.
- Explosions, collisions, footsteps, weather, fire and water.
- Original samples, material selection, positioning and attenuation.
- Falls back to the game's renderer when disabled.
- Optional ModLoader companion for WAV and SFX-pack replacements.
- Alt+Y toggle with an on-screen state message.

## Requirements

- GTA San Andreas 1.0 US (Compact or Hoodlum executable), or a SA-MP
  installation based on it.
- An ASI loader, such as Silent's ASI Loader or Ultimate ASI Loader.
- ModLoader, only for WAV and SFX-pack replacement through the companion.

Other executable versions are left untouched.

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
`AudioRuntime.ini` there together. The companion accepts single WAV files,
complete `FEET`, `GENRL`, `PAIN_A`, `SCRIPT` and `SPC_*` archives and
`BankLkup.dat` from ModLoader mods.

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

## Release Integrity

Releases are built by GitHub Actions from the tagged commit and carry a
SHA-256 file and a build-provenance attestation:

```text
gh attestation verify AudioRuntime-vX.Y.Z.zip -R sonochiwa/sa-audio-runtime
```

## License

MIT. See [LICENSE](LICENSE).
