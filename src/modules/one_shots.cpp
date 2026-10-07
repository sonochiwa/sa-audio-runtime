#include "modules/one_shots.h"

#include "modules/runtime_context.h"
#include "modules/vehicle_capture.h"

namespace runtime {
namespace {

struct OneShotSound {
    void* owner{};
    std::int32_t eventId{};
    std::int16_t bankSlot{};
    std::uintptr_t sourceKey{};
    std::uint32_t endTimeMs{};
};

constexpr std::size_t kMaximumOneShotSounds = 256;
constexpr std::size_t kSoundEventOffset = 0x0C;
constexpr std::size_t kSoundOwnerOffset = 0x04;

using GetSoundBufferFn = const std::uint8_t*(__thiscall*)(
    void*,
    std::int16_t,
    std::int16_t,
    std::uint32_t*,
    std::uint16_t*
);

std::vector<OneShotSound> gOneShotSounds;

std::uint32_t GameTimeMs() {
    return *reinterpret_cast<const volatile std::uint32_t*>(kGameTimeMsAddress);
}

// The length of the sample loaded in the bank slot, which is what the game
// would have played; 0 when the slot holds no such sound.
std::uint32_t SampleLengthMs(std::int16_t soundId, std::int16_t bankSlot) {
    auto* loader = *reinterpret_cast<void* const*>(
        kAudioHardwareAddress + kHardwareBankLoaderOffset
    );
    if (!loader || soundId < 0 || bankSlot < 0) {
        return 0;
    }
    std::uint32_t size{};
    std::uint16_t sampleRate{};
    const auto* buffer = reinterpret_cast<GetSoundBufferFn>(kGetSoundBufferAddress)(
        loader,
        soundId,
        bankSlot,
        &size,
        &sampleRate
    );
    if (!buffer || sampleRate == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(size / sizeof(std::int16_t)) * 1000 / sampleRate
    );
}

bool Matches(
    const OneShotSound& sound,
    void* owner,
    std::int32_t eventId,
    std::int16_t bankSlot
) {
    return (!owner || sound.owner == owner) &&
           (eventId == -1 || sound.eventId == eventId) &&
           (bankSlot == -1 || sound.bankSlot == bankSlot);
}

} // namespace

void TrackOneShotSound(
    const std::uint8_t* request,
    std::uintptr_t sourceKey,
    float speed
) {
    const auto bankSlot = ReadSoundField<std::int16_t>(request, kAeSoundBankSlotOffset);
    const auto lengthMs = SampleLengthMs(
        ReadSoundField<std::int16_t>(request, kAeSoundIdOffset),
        bankSlot
    );
    if (lengthMs == 0) {
        return;
    }
    const auto flags = ReadSoundField<std::uint16_t>(request, kAeSoundFlagsOffset);
    const auto playTime = std::max<std::int16_t>(
        ReadSoundField<std::int16_t>(request, kAeSoundPlayTimeOffset),
        0
    );
    const auto offsetMs = (flags & kSoundStartPercentage) != 0
        ? lengthMs * std::min<std::uint32_t>(playTime, 100) / 100
        : std::min<std::uint32_t>(playTime, lengthMs);
    const auto remainingMs = static_cast<std::uint32_t>(
        static_cast<float>(lengthMs - offsetMs) / std::max(speed, 0.05f)
    );
    if (gOneShotSounds.size() >= kMaximumOneShotSounds) {
        gOneShotSounds.erase(gOneShotSounds.begin());
    }
    gOneShotSounds.push_back({
        ReadSoundField<void*>(request, kSoundOwnerOffset),
        ReadSoundField<std::int32_t>(request, kSoundEventOffset),
        bankSlot,
        sourceKey,
        GameTimeMs() + remainingMs
    });
}

bool HasOneShotSound(void* owner, std::int32_t eventId, std::int16_t bankSlot) {
    const auto now = GameTimeMs();
    return std::any_of(
        gOneShotSounds.begin(),
        gOneShotSounds.end(),
        [&](const OneShotSound& sound) {
            return sound.endTimeMs > now && Matches(sound, owner, eventId, bankSlot);
        }
    );
}

void CancelOneShotSounds(void* owner, std::int32_t eventId, std::int16_t bankSlot) {
    for (auto sound = gOneShotSounds.begin(); sound != gOneShotSounds.end();) {
        if (!Matches(*sound, owner, eventId, bankSlot)) {
            ++sound;
            continue;
        }
        AudioJob job{};
        job.type = AudioJobType::StatefulStop;
        job.sourceKey = sound->sourceKey;
        DialogueBackendEnqueue(job);
        sound = gOneShotSounds.erase(sound);
    }
}

void ExpireOneShotSounds() {
    const auto now = GameTimeMs();
    gOneShotSounds.erase(
        std::remove_if(
            gOneShotSounds.begin(),
            gOneShotSounds.end(),
            [now](const OneShotSound& sound) { return sound.endTimeMs <= now; }
        ),
        gOneShotSounds.end()
    );
}

void ForgetOneShotSounds() {
    gOneShotSounds.clear();
}

} // namespace runtime
