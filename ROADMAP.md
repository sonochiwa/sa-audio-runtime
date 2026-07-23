# Roadmap

SA Audio Runtime moves selected GTA San Andreas sound families to a dedicated
DirectSound worker while GTA remains responsible for event selection and
state changes. Code completion and in-game validation are tracked separately.

## Implemented and validated

- Layered weapon gunshots and persistent minigun playback.
- Bullet impacts selected by GTA for the hit surface.
- Original distance attenuation, positioning, pitch, effects volume and indoor
  weapon tails, including the sniper-rifle echo.
- Runtime enable/disable, pause handling and fallback to GTA's renderer.
- ModLoader replacements for released weapon and impact paths.

## Implemented, validation pending

### Vehicles

- Player and traffic engines, drivetrain, road, flat-tire and reverse loops.
- Skids, horns, sirens and supported vehicle one-shot effects.
- Vehicle creation, destruction and ownership transfer between GTA and the
  runtime.

### Dialogue and general effects

- Positional pedestrian speech, scripted dialogue and police scanner chains.
- Safe untracked one-shots from loaded `FEET`, `GENRL`, `PAIN_A`, `SCRIPT`
  and `SPC_*` banks.

### Explosions and projectiles

- All layered explosion voices, including GTA's forced-front distance pair.
- Grenade and secondary vehicle explosions through the common explosion path.
- Rocket launch layers and entity-following projectile effects.
- A 64-voice stateful limit that rejects or replaces quieter effects under
  dense load.

### Weapon mechanics and loops

- Reload A/B layers and short mechanical weapon effects.
- Dry fire, weapon switch and melee mechanics that use supported GENRL banks.
- Flamethrower, spray can and fire-extinguisher loops.
- Chainsaw idle, active, cutting and stop transitions.
- Original per-frame pitch, volume, timeout and completion callbacks.

### Collisions and vehicle damage

- Material-dependent one-shot and looping collisions.
- Glass hit, crack, break and delayed debris sounds.
- Water entry, splashes, wakes and persistent contact loops.
- Door and garage-door states, object destruction and supported vehicle damage
  effects.

### Characters

- Surface-dependent footsteps, skating and landings.
- Melee swings and impacts, swimming splashes and wakes.
- Jetpack layers, parachute/shirt wind loops and supported movement effects.
- Existing pain, death and speech-bank handling through the dialogue renderer.

### World ambience

- Weather and rain voices, including head-relative twin loops.
- Fire, hydrants, water cannon, waterfall and foghorn voices.
- Supported script-owned persistent emitters and short world effects.

### Shared stateful runtime

- Stable `CAESound` proxies returned to GTA for direct field updates.
- GTA `UpdateParameters` callbacks retained on the main thread.
- Latest-state coalescing for vehicle, dialogue and stateful updates, with
  non-droppable source stops.
- Virtual sources for inaudible persistent sounds, including playback-position
  restoration when they become audible.
- A separate lossless completion path independent of one-shot queue pressure.
- A shared physical-voice scheduler with per-category limits, soft
  reservations and loudness-aware replacement.
- Entity-reference registration, frame delay, sample loop points and doppler.
- Transparent `AreSoundsPlaying` and `CancelSounds` behavior by event, entity,
  physical entity and bank slot.
- Per-module switching and cleanup during pause, reset and termination.
- ModLoader individual-WAV and complete-pack replacements across supported
  runtime banks, including live priority and install-state changes.
- Transactional hook installation and rollback of the custom
  `RequestNewSound` hotpatch on setup failure.
- Automatic DirectSound device recovery with persistent source
  virtualization across device recreation.
- Categorized weapon, vehicle, character and world configuration with
  automatic migration from the former flat module list.
- Focused integration modules for shared game state, weapons, vehicles,
  stateful sounds and runtime bootstrap.

## Required validation

### Stability

- Launch GTA and SA:MP repeatedly and confirm the `RequestNewSound` hotpatch
  does not regress the fixed `0x004EFB14` crash.
- Exercise pause, death, respawn, interior changes and repeated `Alt+Y`
  switching.
- Destroy owners while loops are playing and check for stale voices or crashes.

### Sound parity

- Compare cars, motorcycles, boats, aircraft, trains and special vehicles.
- Compare explosions at near, medium and far distances, indoors and outdoors.
- Check reloads, rockets, chainsaw, flamethrower, spray can and extinguisher.
- Check collisions for cars, objects, glass, water and garage doors.
- Check footsteps on every material, swimming, jetpack and movement loops.
- Check rain, fire, hydrants, waterfalls and script-created ambient emitters.
- Check dialogue and scanner interruption and completion timing.
- Look for missing, doubled, incorrectly centred or permanently looping sounds.

### Compatibility and performance

- Verify ModLoader precedence for individual WAV files, complete packs,
  archive/lookup replacement and live install/uninstall.
- Compare effects volume, pitch, doppler and spatial placement with GTA.
- Run controlled A/B frame-time measurements only after sound parity is
  confirmed.

## Not implemented

- Dedicated treatment of unknown third-party sounds that bypass GTA's standard
  `CAESoundManager` path.
- Version-specific hooks into SA:MP or other multiplayer clients.
- Streaming radio, music, cutscene tracks and SA:MP URL streams.

The streaming categories are intentionally outside project scope: they already
use separate streaming paths and are not part of the frame-time problem this
runtime is intended to solve.

## Release criteria

Every newly supported family must:

1. Match GTA's audible behavior in controlled A/B tests.
2. Survive creation, destruction, pause and runtime switching without stale or
   duplicated voices.
3. Preserve ModLoader replacement priority where applicable.
4. Demonstrate a repeatable frame-time benefit under relevant audio load.
