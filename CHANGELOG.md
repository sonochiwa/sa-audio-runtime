# Changelog

## 1.0.0

- Moved gunshot rendering to a dedicated DirectSound worker to reduce
  main-thread load during crowded SA:MP firefights.
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
