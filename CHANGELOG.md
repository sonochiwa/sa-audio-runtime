# Changelog

## 2.1.1

- Removed the `SA` prefix from the enabled and disabled on-screen status
  messages.

## 2.1.0

- Reworked vehicle sounds with the original game's seamless pre-loop buffer
  layout, removing the restart gap between a horn's attack and loop.
- Fixed rapid start/stop races that could leave a horn or another persistent
  vehicle sound playing behind its owner.
- Restored the original short software fades for stopped sounds and large
  volume transitions, including skid twin-loop swaps.
- Corrected persistent vehicle sound ownership and acceleration-layer playback
  timing by using each sample's actual duration.
- Added a GitHub Actions release workflow that builds release binaries from
  tagged commits.
- Added SHA-256 checksum files and signed GitHub artifact attestations so
  downloaded release archives can be verified against their source workflow.

## 2.0.0

- Expanded worker-thread audio rendering to vehicles, explosions, weapon
  effects, collisions, character sounds, dialogue, the police scanner and
  world ambience.
- Improved performance and stability during heavy audio load, including
  automatic recovery after DirectSound device loss.
- Expanded ModLoader support for individual WAV replacements and complete SFX
  packs, and fixed nested `bank_*\sound_*.wav` folders not being detected.
- Added separate configuration sections for weapon, vehicle, character and
  world audio so supported sound groups can be enabled independently.
- Changed INI section names to `lowerCamelCase` while keeping old
  configurations compatible and automatically migrating the former
  `[Modules]` section.
- Allowed the runtime hotkey to be disabled with `hotkeyEnabled=0` or by
  removing or zeroing `hotkeyKey`.

## 1.1.0

- Moved bullet-impact sounds to the worker renderer with GTA's original
  material selection, volume, pitch variation and distance attenuation.
- Added impact-voice prioritization to keep sustained automatic fire bounded
  without masking louder nearby hits.
- Reworked minigun playback as persistent worker-owned fire, spin and stop
  states to reduce repeated main-thread audio work.
- Restored the stereo width of forced-front weapon tails, including the
  sniper-rifle echo.
- Added ModLoader WAV replacement support for the bullet-impact GENRL bank.

## 1.0.0

- Moved gunshot rendering to a dedicated DirectSound worker to reduce
  main-thread load during crowded firefights.
- Reproduced GTA's original GENRL samples, layers, pitch variation, spatial
  placement, distance attenuation, indoor tails and effects-volume response.
- Added ModLoader weapon WAV replacement support with live priority and
  install-state updates.
- Added persistent configuration and an `Alt+Y` runtime toggle with on-screen
  status notifications.
- Added automatic fallback to GTA's original gunshot renderer when the runtime
  is disabled or unavailable.
- Added game pause handling, DirectSound buffer-loss recovery and validation
  for GTA SA 1.0 US Compact and Hoodlum executables.
