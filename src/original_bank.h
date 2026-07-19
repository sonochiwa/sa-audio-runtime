#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct OriginalPcmSample {
    std::vector<std::uint8_t> pcm;
    std::uint32_t sampleRate{};
    std::int16_t headroom{};
};

class OriginalWeaponBank {
public:
    bool Load(const std::string& gameDirectory, std::string& error);
    bool ApplyWaveOverride(
        std::int16_t soundId,
        const std::string& path,
        std::string& error
    );
    bool RestoreOriginal(std::int16_t soundId);
    const OriginalPcmSample* Get(std::int16_t soundId) const;

private:
    std::array<OriginalPcmSample, 400> mSamples{};
    std::array<OriginalPcmSample, 400> mOriginalSamples{};
    std::size_t mSampleCount{};
};
