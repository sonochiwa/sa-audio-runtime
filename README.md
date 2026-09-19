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
- Switched on and off by typing `AUDIORUNTIME` in game, with an on-screen
  state message.
- Verifies the bytes it replaces before writing and refuses to patch any
  other executable.

## Requirements

- GTA San Andreas 1.0 US (Compact or Hoodlum executable), or a SA-MP
  installation based on it.
- An ASI loader, such as Silent's ASI Loader or Ultimate ASI Loader.
- ModLoader, only for WAV and SFX-pack replacement through the companion.

Other executables are left untouched.

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

`AudioRuntime.ini` next to the plugin, created with these defaults when it is
missing:

| Setting | Default | Meaning |
| --- | ---: | --- |
| `isEnabled` | `1` | Runtime state. Written back when the command toggles it. |
| `command` | `AUDIORUNTIME` | Word that switches the runtime on or off when typed in game. Empty disables it. |

Type the command word in game the way a single-player cheat is typed, with
the chat box open or closed. The new state is shown on screen, written to
`isEnabled` and restored on the next launch.

## Release Integrity

Releases are built by GitHub Actions from the tagged commit and carry a
SHA-256 file and a build-provenance attestation:

```text
gh attestation verify AudioRuntime-vX.Y.Z.zip -R sonochiwa/sa-audio-runtime
```

## License

MIT. See [LICENSE](LICENSE).
