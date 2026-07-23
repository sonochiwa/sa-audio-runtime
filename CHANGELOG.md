# Changelog

## 2.0.0

- Added worker-owned vehicle engines, drivetrain, road noise, tire loops,
  reverse, skids, horns, sirens and supported vehicle one-shot effects.
- Added stateful rendering for supported explosions, weapon mechanics,
  collisions, character effects and world ambience while keeping GTA's
  `UpdateParameters` callbacks on the main thread.
- Added positional dialogue and police-scanner proxy lifecycles.
- Extended ModLoader support to individual WAV files and complete supported
  SFX packs with live priority and install-state updates.
- Added latest-state coalescing for persistent sources so obsolete per-frame
  updates cannot accumulate in the one-shot queue.
- Added inaudible-source virtualization with playback-position restoration and
  hysteresis when a source becomes audible again.
- Added non-droppable source stops and a completion queue independent of
  one-shot queue pressure.
- Added a shared 96-voice scheduler with category limits, soft reservations
  and loudness-aware voice stealing across weapons, vehicles and runtime
  effects.
- Added automatic DirectSound device recovery while preserving persistent
  source state and playback position.
- Fixed pending and suspended voices surviving category shutdown.
- Added transactional hook installation and rollback for the custom
  `RequestNewSound` hotpatch.
- Split runtime configuration into weapon, vehicle, character and world audio
  sections. Gunshots, bullet impacts, vehicle engines and vehicle effects can
  now be switched independently.
- Added automatic migration from the former `[Modules]` configuration section.
- Split the GTA integration layer into focused source modules while preserving
  a single internal translation unit.
- Reduced backend thread stack usage and moved long-running clocks to
  `GetTickCount64`.

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
