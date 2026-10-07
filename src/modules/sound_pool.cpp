#include "modules/sound_pool.h"

#include <cstring>
#include <deque>
#include <memory>
#include <vector>

namespace runtime {
namespace {

constexpr std::size_t kSlotsPerChunk = 256;
// Released slots wait this long in line before one is reused; the game's late
// calls through an ended sound come within a few frames.
constexpr std::size_t kReuseDelaySlots = 512;

struct Slot {
    alignas(4) std::uint8_t bytes[kPooledSoundSize];
};

struct Pool {
    std::vector<std::unique_ptr<Slot[]>> chunks;
    std::size_t usedInLastChunk{kSlotsPerChunk};
    std::deque<std::uint8_t*> released;
};

// Never destroyed: proxies in static maps release their slots during process
// exit, after a pool with static storage could already be gone.
Pool& GetPool() {
    static auto* pool = new Pool;
    return *pool;
}

std::uint8_t* Acquire() {
    auto& pool = GetPool();
    if (pool.released.size() > kReuseDelaySlots) {
        auto* bytes = pool.released.front();
        pool.released.pop_front();
        return bytes;
    }
    if (pool.usedInLastChunk == kSlotsPerChunk) {
        pool.chunks.push_back(std::make_unique<Slot[]>(kSlotsPerChunk));
        pool.usedInLastChunk = 0;
    }
    return pool.chunks.back()[pool.usedInLastChunk++].bytes;
}

} // namespace

PooledSound::PooledSound() : bytes(Acquire()) {
    std::memset(bytes, 0, kPooledSoundSize);
}

PooledSound::~PooledSound() {
    std::memset(bytes, 0, kPooledSoundSize);
    GetPool().released.push_back(bytes);
}

} // namespace runtime
