#include "modules/modules.h"

namespace runtime {

bool StatefulSoundMatches(
    const StatefulSoundProxy& proxy,
    void* owner,
    std::int32_t eventId,
    void* physicalEntity,
    std::int16_t bankSlot
) {
    if (!proxy.active) {
        return false;
    }
    if (owner && proxy.owner != owner) {
        return false;
    }
    if (eventId != -1 &&
        ReadSoundField<std::int32_t>(
            proxy.sound.data(),
            0x0C
        ) != eventId) {
        return false;
    }
    if (physicalEntity &&
        ReadSoundField<void*>(
            proxy.sound.data(),
            kAeSoundPhysicalEntityOffset
        ) != physicalEntity) {
        return false;
    }
    if (bankSlot != -1 &&
        ReadSoundField<std::int16_t>(
            proxy.sound.data(),
            kAeSoundBankSlotOffset
        ) != bankSlot) {
        return false;
    }
    return true;
}

bool HasStatefulSound(
    void* owner,
    std::int16_t eventId,
    void* physicalEntity
) {
    return std::any_of(
        gStatefulSoundProxies.begin(),
        gStatefulSoundProxies.end(),
        [=](const auto& entry) {
            return StatefulSoundMatches(
                entry.second,
                owner,
                eventId,
                physicalEntity,
                -1
            );
        }
    );
}

bool HasStatefulSoundInBank(std::int16_t bankSlot) {
    return std::any_of(
        gStatefulSoundProxies.begin(),
        gStatefulSoundProxies.end(),
        [=](const auto& entry) {
            return StatefulSoundMatches(
                entry.second,
                nullptr,
                -1,
                nullptr,
                bankSlot
            );
        }
    );
}

void CancelStatefulSounds(
    void* owner,
    std::int32_t eventId,
    void* physicalEntity,
    std::int16_t bankSlot,
    bool forget
) {
    for (auto proxy = gStatefulSoundProxies.begin();
         proxy != gStatefulSoundProxies.end();) {
        auto& value = proxy->second;
        if (!StatefulSoundMatches(
                value,
                owner,
                eventId,
                physicalEntity,
                bankSlot
            )) {
            ++proxy;
            continue;
        }
        if (forget) {
            auto flags = ReadSoundField<std::uint16_t>(
                value.sound.data(),
                kAeSoundFlagsOffset
            );
            flags &= static_cast<std::uint16_t>(~kSoundRequestUpdates);
            std::memcpy(
                value.sound.data() + kAeSoundFlagsOffset,
                &flags,
                sizeof(flags)
            );
            value.owner = nullptr;
            void* noOwner{};
            std::memcpy(
                value.sound.data() + 4,
                &noOwner,
                sizeof(noOwner)
            );
        } else {
            reinterpret_cast<void(__thiscall*)(void*)>(
                kStopSoundAddress
            )(value.sound.data());
            ++proxy;
            continue;
        }
        StopStatefulSound(value);
        FinishStatefulSound(value);
        proxy = gStatefulSoundProxies.erase(proxy);
    }
}

std::int16_t __fastcall HookAreEventSoundsPlaying(
    void* self,
    void*,
    std::int16_t eventId,
    void* owner
) {
    const auto original =
        gOriginalAreEventSoundsPlaying(self, eventId, owner);
    if (original != 0) {
        return original;
    }
    return HasStatefulSound(owner, eventId, nullptr) ? 2 : 0;
}

std::int16_t __fastcall HookAreBankSoundsPlaying(
    void* self,
    void*,
    std::int16_t bankSlot
) {
    const auto original =
        gOriginalAreBankSoundsPlaying(self, bankSlot);
    if (original != 0) {
        return original;
    }
    return HasStatefulSoundInBank(bankSlot) ? 2 : 0;
}

std::int16_t __fastcall HookAreEventPhysicalSoundsPlaying(
    void* self,
    void*,
    std::int16_t eventId,
    void* owner,
    void* physicalEntity
) {
    const auto original = gOriginalAreEventPhysicalSoundsPlaying(
        self,
        eventId,
        owner,
        physicalEntity
    );
    if (original != 0) {
        return original;
    }
    return HasStatefulSound(owner, eventId, physicalEntity) ? 2 : 0;
}

} // namespace runtime
