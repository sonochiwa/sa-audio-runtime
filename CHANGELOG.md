# Changelog

## 2.4.0

- Changed the toggle to a word typed in game, `AUDIORUNTIME` by default;
  the hotkey keys are gone.
- Added `README.txt` to the release archive.

## 2.3.0

- Removed the per-module switches and `showNotifications`; only the toggle
  hotkey remains and the state message is always shown.

## 2.2.0

- Changed the public name to Audio Runtime.
- Changed the INI to be created from the file compiled into the plugin.
- Added version information to both plugin files.
- Removed `README.txt` from the release archive.

## 2.1.2

- Fixed clicks at the start of vehicle loops and on rapid horn taps.
- Fixed loading tunes and other managed sounds not stopping the way the game
  stops them.
- Fixed worker sounds surviving an audio engine reset.

## 2.1.1

- Removed the `SA` prefix from the on-screen status messages.

## 2.1.0

- Fixed the gap between a horn's attack and its loop.
- Fixed a horn or another persistent vehicle sound playing on after its
  owner stopped.
- Restored the short fades on stopped sounds and large volume changes.
- Fixed vehicle sound ownership and acceleration-layer timing.
- Added the GitHub Actions release workflow with checksums and attestation.

## 2.0.0

- Added worker rendering for vehicles, explosions, weapon effects,
  collisions, character sounds, dialogue, the police scanner and ambience.
- Improved stability under heavy load, with recovery after device loss.
- Added ModLoader support for complete SFX packs.
- Added per-family switches in the INI; old `[Modules]` files are migrated.
- Added `hotkeyEnabled=0` to disable the hotkey.

## 1.1.0

- Added bullet impacts with the original material selection and
  attenuation, and impact-voice prioritisation under automatic fire.
- Reworked minigun playback as persistent worker-owned states.
- Restored the stereo width of weapon tails, including the sniper echo.
- Added ModLoader WAV replacement for the bullet-impact bank.

## 1.0.0

- Gunshots rendered on a dedicated DirectSound worker with the game's
  original samples, layers, positioning and attenuation.
- ModLoader weapon WAV replacement.
- Alt+Y toggle with an on-screen message; falls back to the game's renderer
  when disabled.
