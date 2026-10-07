#pragma once

#include "modules/prelude.h"

namespace runtime {

// One-shot sounds the worker plays in place of the game return no sound to
// the game, so its "are sounds of this event playing" and cancel queries
// would not see them. Entities rely on those queries to play a sound once
// per event (a ped's crunch, the missile lock tone); each one-shot is kept
// here for the length of its sample.
void TrackOneShotSound(
    const std::uint8_t* request,
    std::uintptr_t sourceKey,
    float speed
);
bool HasOneShotSound(void* owner, std::int32_t eventId, std::int16_t bankSlot);
void CancelOneShotSounds(void* owner, std::int32_t eventId, std::int16_t bankSlot);
void ExpireOneShotSounds();
void ForgetOneShotSounds();

} // namespace runtime
