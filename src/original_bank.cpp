#include "original_bank.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>

namespace {

constexpr std::size_t kWeaponBankId = 143;
constexpr std::size_t kBankLookupEntrySize = 12;
constexpr std::size_t kMaxSounds = 400;
constexpr std::size_t kSoundInfoSize = 12;
constexpr std::size_t kBankHeaderSize = 4 + kMaxSounds * kSoundInfoSize;

std::uint16_t ReadU16(const std::uint8_t* data) {
    std::uint16_t value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

std::int16_t ReadI16(const std::uint8_t* data) {
    std::int16_t value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

std::uint32_t ReadU32(const std::uint8_t* data) {
    std::uint32_t value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool ReadExact(
    std::ifstream& stream,
    void* destination,
    std::size_t size
) {
    if (size > static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max()
        )) {
        return false;
    }
    stream.read(
        static_cast<char*>(destination),
        static_cast<std::streamsize>(size)
    );
    return stream.good() || static_cast<std::size_t>(stream.gcount()) == size;
}

} // namespace

bool OriginalWeaponBank::Load(
    const std::string& gameDirectory,
    std::string& error
) {
    mSampleCount = 0;
    for (auto& sample : mSamples) {
        sample = {};
    }

    const auto lookupPath = gameDirectory + "\\audio\\CONFIG\\BankLkup.dat";
    std::ifstream lookup(lookupPath, std::ios::binary);
    if (!lookup) {
        error = "cannot open " + lookupPath;
        return false;
    }

    std::array<std::uint8_t, kBankLookupEntrySize> entry{};
    lookup.seekg(
        static_cast<std::streamoff>(kWeaponBankId * kBankLookupEntrySize),
        std::ios::beg
    );
    if (!ReadExact(lookup, entry.data(), entry.size())) {
        error = "cannot read weapon bank lookup entry";
        return false;
    }

    const auto pakId = entry[0];
    const auto bankOffset = ReadU32(entry.data() + 4);
    const auto bankDataSize = ReadU32(entry.data() + 8);
    if (pakId != 1 || bankDataSize == 0) {
        error = "weapon bank is not in the original GENRL pak";
        return false;
    }

    const auto genrlPath = gameDirectory + "\\audio\\SFX\\GENRL";
    std::ifstream genrl(genrlPath, std::ios::binary);
    if (!genrl) {
        error = "cannot open " + genrlPath;
        return false;
    }

    std::array<std::uint8_t, kBankHeaderSize> header{};
    genrl.seekg(static_cast<std::streamoff>(bankOffset), std::ios::beg);
    if (!ReadExact(genrl, header.data(), header.size())) {
        error = "cannot read GENRL weapon bank header";
        return false;
    }

    const auto numSounds = ReadU16(header.data());
    if (numSounds == 0 || numSounds > kMaxSounds) {
        error = "invalid weapon bank sound count";
        return false;
    }

    std::vector<std::uint8_t> bankData(bankDataSize);
    if (!ReadExact(genrl, bankData.data(), bankData.size())) {
        error = "cannot read GENRL weapon bank PCM data";
        return false;
    }

    for (std::size_t id = 0; id < numSounds; ++id) {
        const auto* info = header.data() + 4 + id * kSoundInfoSize;
        const auto offset = ReadU32(info);
        const auto nextOffset = id + 1 < numSounds
            ? ReadU32(info + kSoundInfoSize)
            : bankDataSize;
        const auto sampleRate = ReadU16(info + 8);
        const auto headroom = ReadI16(info + 10);

        if (offset > nextOffset || nextOffset > bankData.size() ||
            sampleRate < 100 || ((nextOffset - offset) & 1u) != 0) {
            error = "invalid PCM metadata in GENRL weapon bank";
            return false;
        }

        auto& sample = mSamples[id];
        sample.sampleRate = sampleRate;
        sample.headroom = headroom;
        sample.pcm.assign(
            bankData.begin() + offset,
            bankData.begin() + nextOffset
        );
    }

    mSampleCount = numSounds;
    mOriginalSamples = mSamples;
    return true;
}

bool OriginalWeaponBank::ApplyWaveOverride(
    std::int16_t soundId,
    const std::string& path,
    std::string& error
) {
    if (soundId < 0 || static_cast<std::size_t>(soundId) >= mSampleCount) {
        error = "override sound id is outside the weapon bank";
        return false;
    }

    std::ifstream wave(path, std::ios::binary);
    if (!wave) {
        error = "cannot open override " + path;
        return false;
    }
    std::array<std::uint8_t, 12> riff{};
    if (!ReadExact(wave, riff.data(), riff.size()) ||
        std::memcmp(riff.data(), "RIFF", 4) != 0 ||
        std::memcmp(riff.data() + 8, "WAVE", 4) != 0) {
        error = "override is not a RIFF WAVE";
        return false;
    }

    std::uint16_t formatTag{};
    std::uint16_t channels{};
    std::uint16_t bitsPerSample{};
    std::uint32_t sampleRate{};
    std::vector<std::uint8_t> pcm;
    while (wave) {
        std::array<std::uint8_t, 8> chunk{};
        if (!ReadExact(wave, chunk.data(), chunk.size())) {
            break;
        }
        const auto chunkSize = ReadU32(chunk.data() + 4);
        if (std::memcmp(chunk.data(), "fmt ", 4) == 0) {
            if (chunkSize < 16 || chunkSize > 4096) {
                error = "invalid override fmt chunk";
                return false;
            }
            std::vector<std::uint8_t> format(chunkSize);
            if (!ReadExact(wave, format.data(), format.size())) {
                error = "truncated override fmt chunk";
                return false;
            }
            formatTag = ReadU16(format.data());
            channels = ReadU16(format.data() + 2);
            sampleRate = ReadU32(format.data() + 4);
            bitsPerSample = ReadU16(format.data() + 14);
        } else if (std::memcmp(chunk.data(), "data", 4) == 0) {
            pcm.resize(chunkSize);
            if (!ReadExact(wave, pcm.data(), pcm.size())) {
                error = "truncated override data chunk";
                return false;
            }
        } else {
            wave.seekg(
                static_cast<std::streamoff>(chunkSize),
                std::ios::cur
            );
        }
        if (chunkSize & 1U) {
            wave.seekg(1, std::ios::cur);
        }
    }
    if (formatTag != 1 || channels != 1 || bitsPerSample != 16 ||
        sampleRate < 100 || pcm.empty() || (pcm.size() & 1U)) {
        error = "override must be mono PCM 16-bit";
        return false;
    }

    auto& sample = mSamples[static_cast<std::size_t>(soundId)];
    sample.pcm = std::move(pcm);
    sample.sampleRate = sampleRate;
    // WAV overrides inherit the original bank headroom.
    sample.headroom =
        mOriginalSamples[static_cast<std::size_t>(soundId)].headroom;
    return true;
}

bool OriginalWeaponBank::RestoreOriginal(std::int16_t soundId) {
    if (soundId < 0 || static_cast<std::size_t>(soundId) >= mSampleCount) {
        return false;
    }
    mSamples[static_cast<std::size_t>(soundId)] =
        mOriginalSamples[static_cast<std::size_t>(soundId)];
    return true;
}

const OriginalPcmSample* OriginalWeaponBank::Get(std::int16_t soundId) const {
    if (soundId < 0 || static_cast<std::size_t>(soundId) >= mSampleCount) {
        return nullptr;
    }
    return &mSamples[static_cast<std::size_t>(soundId)];
}
