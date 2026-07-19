# Changelog

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
