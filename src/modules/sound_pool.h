#pragma once

#include <cstddef>
#include <cstdint>

namespace runtime {

constexpr std::size_t kPooledSoundSize = 0x74;

// The CAESound copy a runtime proxy hands to the game in place of one of the
// game's own sound slots. The game keeps such pointers after a sound has
// ended (a vehicle's horn, the collision list, a fire, a ped's speech) and
// later stops the sound or unregisters it from its entity through them, as
// it does with its own slots, which live in a static array. These slots are
// never freed either: a released slot is zeroed, so such a late call finds no
// entity and no owner, and a slot is reused only after many others were
// released, when no late call through it is pending any more.
class PooledSound {
public:
    PooledSound();
    ~PooledSound();
    PooledSound(const PooledSound&) = delete;
    PooledSound& operator=(const PooledSound&) = delete;

    std::uint8_t* data() { return bytes; }
    const std::uint8_t* data() const { return bytes; }
    constexpr std::size_t size() const { return kPooledSoundSize; }

private:
    std::uint8_t* bytes;
};

} // namespace runtime
