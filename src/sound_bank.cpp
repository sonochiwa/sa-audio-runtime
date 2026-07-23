#include "sound_bank.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>

namespace {

constexpr std::size_t kBankLookupEntrySize = 12;
constexpr std::size_t kPakLookupEntrySize = 52;
constexpr std::size_t kPakFilenameSize = 12;
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

bool OriginalSoundBank::Load(
    const std::string& gameDirectory,
    std::size_t bankId,
    std::string& error
) {
    const auto lookupPath =
        gameDirectory + "\\audio\\CONFIG\\BankLkup.dat";
    std::ifstream lookup(lookupPath, std::ios::binary);
    if (!lookup) {
        error = "cannot open " + lookupPath;
        return false;
    }
    std::array<std::uint8_t, kBankLookupEntrySize> bankEntry{};
    lookup.seekg(
        static_cast<std::streamoff>(bankId * kBankLookupEntrySize),
        std::ios::beg
    );
    if (!ReadExact(lookup, bankEntry.data(), bankEntry.size())) {
        error = "cannot read sound bank lookup entry";
        return false;
    }

    const auto pakLookupPath =
        gameDirectory + "\\audio\\CONFIG\\PakFiles.dat";
    std::ifstream pakLookup(pakLookupPath, std::ios::binary);
    if (!pakLookup) {
        error = "cannot open " + pakLookupPath;
        return false;
    }
    std::array<std::uint8_t, kPakLookupEntrySize> pakEntry{};
    pakLookup.seekg(
        static_cast<std::streamoff>(
            bankEntry[0] * kPakLookupEntrySize
        ),
        std::ios::beg
    );
    if (!ReadExact(pakLookup, pakEntry.data(), pakEntry.size())) {
        error = "cannot read sound pack lookup entry";
        return false;
    }
    std::string pakFilename;
    for (std::size_t index = 0; index < kPakFilenameSize; ++index) {
        const auto character = pakEntry[index];
        if (character == 0 || character == 0xCD) {
            break;
        }
        pakFilename.push_back(static_cast<char>(character));
    }
    if (pakFilename.empty()) {
        error = "sound pack filename is empty";
        return false;
    }
    return Load(
        lookupPath,
        gameDirectory + "\\audio\\SFX\\" + pakFilename,
        bankId,
        error
    );
}

bool OriginalSoundBank::Load(
    const std::string& lookupPath,
    const std::string& archivePath,
    std::size_t bankId,
    std::string& error
) {
    mSampleCount = 0;
    for (auto& sample : mSamples) {
        sample = {};
    }
    for (auto& sample : mOriginalSamples) {
        sample = {};
    }

    std::ifstream lookup(lookupPath, std::ios::binary);
    if (!lookup) {
        error = "cannot open " + lookupPath;
        return false;
    }

    std::array<std::uint8_t, kBankLookupEntrySize> entry{};
    lookup.seekg(
        static_cast<std::streamoff>(bankId * kBankLookupEntrySize),
        std::ios::beg
    );
    if (!ReadExact(lookup, entry.data(), entry.size())) {
        error = "cannot read sound bank lookup entry";
        return false;
    }

    const auto bankOffset = ReadU32(entry.data() + 4);
    const auto bankDataSize = ReadU32(entry.data() + 8);
    if (bankDataSize == 0) {
        error = "sound bank is empty";
        return false;
    }

    std::ifstream genrl(archivePath, std::ios::binary);
    if (!genrl) {
        error = "cannot open " + archivePath;
        return false;
    }

    std::array<std::uint8_t, kBankHeaderSize> header{};
    genrl.seekg(static_cast<std::streamoff>(bankOffset), std::ios::beg);
    if (!ReadExact(genrl, header.data(), header.size())) {
        error = "cannot read GENRL sound bank header";
        return false;
    }

    const auto numSounds = ReadU16(header.data());
    if (numSounds == 0 || numSounds > kMaxSounds) {
        error = "invalid sound bank sound count";
        return false;
    }

    std::vector<std::uint8_t> bankData(bankDataSize);
    if (!ReadExact(genrl, bankData.data(), bankData.size())) {
        error = "cannot read GENRL sound bank PCM data";
        return false;
    }

    for (std::size_t id = 0; id < numSounds; ++id) {
        const auto* info = header.data() + 4 + id * kSoundInfoSize;
        const auto offset = ReadU32(info);
        const auto loopStartSample = static_cast<std::int32_t>(
            ReadU32(info + 4)
        );
        const auto nextOffset = id + 1 < numSounds
            ? ReadU32(info + kSoundInfoSize)
            : bankDataSize;
        const auto sampleRate = ReadU16(info + 8);
        const auto headroom = ReadI16(info + 10);

        if (offset > nextOffset || nextOffset > bankData.size() ||
            sampleRate < 100 || ((nextOffset - offset) & 1u) != 0) {
            error = "invalid PCM metadata in GENRL sound bank";
            return false;
        }

        auto& sample = mSamples[id];
        sample.sampleRate = sampleRate;
        sample.headroom = headroom;
        sample.loopStartSample = loopStartSample;
        sample.pcm.assign(
            bankData.begin() + offset,
            bankData.begin() + nextOffset
        );
    }

    mSampleCount = numSounds;
    return true;
}

bool OriginalSoundBank::ApplyWaveOverride(
    std::int16_t soundId,
    const std::string& path,
    std::string& error
) {
    if (soundId < 0 || static_cast<std::size_t>(soundId) >= mSampleCount) {
        error = "override sound id is outside the sound bank";
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

    const auto index = static_cast<std::size_t>(soundId);
    auto& sample = mSamples[index];
    auto& original = mOriginalSamples[index];
    if (original.sampleRate == 0) {
        original = sample;
    }
    sample.pcm = std::move(pcm);
    sample.sampleRate = sampleRate;
    sample.headroom = original.headroom;
    const auto originalLoop = original.loopStartSample;
    sample.loopStartSample =
        originalLoop >= 0 &&
        static_cast<std::size_t>(originalLoop) < sample.pcm.size() / 2
            ? originalLoop
            : (originalLoop >= 0 ? 0 : -1);
    return true;
}

bool OriginalSoundBank::RestoreOriginal(std::int16_t soundId) {
    if (soundId < 0 || static_cast<std::size_t>(soundId) >= mSampleCount) {
        return false;
    }
    const auto index = static_cast<std::size_t>(soundId);
    if (mOriginalSamples[index].sampleRate != 0) {
        mSamples[index] = mOriginalSamples[index];
    }
    return true;
}

const OriginalPcmSample* OriginalSoundBank::Get(std::int16_t soundId) const {
    if (soundId < 0 || static_cast<std::size_t>(soundId) >= mSampleCount) {
        return nullptr;
    }
    return &mSamples[static_cast<std::size_t>(soundId)];
}
