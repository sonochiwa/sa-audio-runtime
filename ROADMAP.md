# Roadmap

SA Audio Runtime 1.0.0 implements gunshot rendering only. Future modules will
be added only when profiling shows a measurable main-thread cost and their
playback can be reproduced without breaking compatibility.

| Priority | Audio family | Status |
| --- | --- | --- |
| 1 | Gunshots | Available in 1.0.0 |
| 2 | Bullet impacts and weapon mechanics | Profiling candidate |
| 3 | Vehicle engines and drivetrain | Profiling candidate |
| 4 | Vehicle tires, sirens, collisions and damage | Planned |
| 5 | World ambience and persistent environmental voices | Planned |
| 6 | Character, frontend and scripted effects | Low priority |
| 7 | Radio, music and dialogue streams | Out of scope for now |

Vehicle and environmental audio require stateful voice management, entity
lifetime tracking and virtualization of distant or inaudible loops. They cannot
reuse the one-shot gunshot queue unchanged.

Before release, every module must:

1. Have its original GTA call path and playback parameters understood.
2. Match the original sound in controlled A/B tests.
3. Demonstrate a measurable frame-time benefit under repeatable load.
4. Return ownership to GTA without double playback or stale voices when
   disabled.
