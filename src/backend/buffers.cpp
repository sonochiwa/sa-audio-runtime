#include "backend/backend.h"

namespace backend {

HWND FindProcessWindow() {
    struct Search {
        DWORD processId;
        HWND window;
    } search{GetCurrentProcessId(), nullptr};

    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            auto& state = *reinterpret_cast<Search*>(parameter);
            DWORD processId{};
            GetWindowThreadProcessId(window, &processId);
            if (processId == state.processId && IsWindowVisible(window) &&
                GetWindow(window, GW_OWNER) == nullptr) {
                state.window = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search)
    );
    return search.window;
}

bool CreateBaseBuffer(
    IDirectSound8* directSound,
    const OriginalPcmSample& sample,
    IDirectSoundBuffer** output
) {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = sample.sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    DSBUFFERDESC description{};
    description.dwSize = sizeof(description);
    description.dwFlags =
        DSBCAPS_CTRLVOLUME |
        DSBCAPS_CTRL3D |
        DSBCAPS_CTRLFREQUENCY |
        DSBCAPS_GLOBALFOCUS |
        DSBCAPS_GETCURRENTPOSITION2 |
        DSBCAPS_LOCSOFTWARE;
    description.dwBufferBytes = static_cast<DWORD>(sample.pcm.size());
    description.lpwfxFormat = &format;

    IDirectSoundBuffer* buffer{};
    if (FAILED(directSound->CreateSoundBuffer(&description, &buffer, nullptr))) {
        return false;
    }

    void* first{};
    void* second{};
    DWORD firstSize{};
    DWORD secondSize{};
    if (FAILED(buffer->Lock(
            0,
            description.dwBufferBytes,
            &first,
            &firstSize,
            &second,
            &secondSize,
            0
        ))) {
        buffer->Release();
        return false;
    }

    std::memcpy(first, sample.pcm.data(), firstSize);
    if (second && secondSize) {
        std::memcpy(
            second,
            sample.pcm.data() + firstSize,
            secondSize
        );
    }
    buffer->Unlock(first, firstSize, second, secondSize);
    *output = buffer;
    return true;
}

bool CreatePreloopBuffer(
    IDirectSound8* directSound,
    const OriginalPcmSample& sample,
    IDirectSoundBuffer** output,
    DWORD& initialPosition,
    DWORD& rewriteOffset,
    DWORD& rewriteBytes
) {
    constexpr std::size_t kMinimumLoopBufferBytes = 24000;
    if (sample.loopStartSample <= 0) {
        return false;
    }
    const auto preloopBytes =
        static_cast<std::size_t>(sample.loopStartSample) * 2;
    if (preloopBytes >= sample.pcm.size()) {
        return false;
    }
    const auto loopBytes = sample.pcm.size() - preloopBytes;
    if (loopBytes == 0) {
        return false;
    }

    const auto targetBytes = std::max(
        sample.pcm.size(),
        kMinimumLoopBufferBytes
    );
    const auto loopCopies = targetBytes / loopBytes + 1;
    if (loopCopies >
        static_cast<std::size_t>(MAXDWORD) / loopBytes) {
        return false;
    }
    const auto bufferBytes = loopCopies * loopBytes;
    if (bufferBytes < preloopBytes ||
        bufferBytes > static_cast<std::size_t>(MAXDWORD)) {
        return false;
    }

    OriginalPcmSample prepared = sample;
    prepared.loopStartSample = 0;
    prepared.pcm.resize(bufferBytes);
    for (std::size_t offset = 0; offset < bufferBytes; ++offset) {
        prepared.pcm[offset] =
            sample.pcm[preloopBytes + offset % loopBytes];
    }

    const auto preloopOffset = bufferBytes - preloopBytes;
    std::memcpy(
        prepared.pcm.data() + preloopOffset,
        sample.pcm.data(),
        preloopBytes
    );
    if (!CreateBaseBuffer(directSound, prepared, output)) {
        return false;
    }

    initialPosition = static_cast<DWORD>(preloopOffset);
    rewriteOffset = static_cast<DWORD>(preloopOffset);
    rewriteBytes = static_cast<DWORD>(preloopBytes);
    return true;
}

bool EnsureBaseBuffer(
    IDirectSound8* directSound,
    const OriginalPcmSample& sample,
    IDirectSoundBuffer*& buffer
) {
    if (buffer) {
        DWORD status{};
        if (SUCCEEDED(buffer->GetStatus(&status)) &&
            !(status & DSBSTATUS_BUFFERLOST)) {
            return true;
        }
        buffer->Release();
        buffer = nullptr;
    }
    return CreateBaseBuffer(directSound, sample, &buffer);
}

void ApplyPendingOverrides(
    RuntimeSoundBank runtimeBank,
    OriginalSoundBank& bank,
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers
) {
    const auto bankIndex = static_cast<std::size_t>(runtimeBank);
    std::array<std::string, kMaxOriginalSounds> paths{};
    std::array<std::int8_t, kMaxOriginalSounds> actions{};
    AcquireSRWLockExclusive(&gOverrideLock);
    for (std::size_t index = 0; index < kMaxOriginalSounds; ++index) {
        if (gOverrideActions[bankIndex][index] != 0) {
            actions[index] = gOverrideActions[bankIndex][index];
            paths[index] = std::move(gOverridePaths[bankIndex][index]);
            gOverrideActions[bankIndex][index] = 0;
        }
    }
    ReleaseSRWLockExclusive(&gOverrideLock);

    for (std::size_t index = 0; index < kMaxOriginalSounds; ++index) {
        if (actions[index] == 0) {
            continue;
        }
        bool changed{};
        std::string error;
        if (actions[index] > 0) {
            changed = bank.ApplyWaveOverride(
                static_cast<std::int16_t>(index),
                paths[index],
                error
            );
        } else {
            changed = bank.RestoreOriginal(
                static_cast<std::int16_t>(index)
            );
        }
        if (changed && baseBuffers[index]) {
            baseBuffers[index]->Release();
            baseBuffers[index] = nullptr;
        }
    }
}

void ApplyCurrentOverrides(
    RuntimeSoundBank runtimeBank,
    OriginalSoundBank& bank
) {
    const auto bankIndex = static_cast<std::size_t>(runtimeBank);
    std::array<std::string, kMaxOriginalSounds> paths{};
    AcquireSRWLockShared(&gOverrideLock);
    paths = gOverridePaths[bankIndex];
    ReleaseSRWLockShared(&gOverrideLock);

    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (paths[index].empty()) {
            continue;
        }
        std::string error;
        bank.ApplyWaveOverride(
            static_cast<std::int16_t>(index),
            paths[index],
            error
        );
    }
}

bool TakeBankSourceUpdate(
    std::string& archivePath,
    std::string& lookupPath
) {
    AcquireSRWLockExclusive(&gOverrideLock);
    const bool changed = gBankSourcesDirty;
    if (changed) {
        archivePath = gArchiveOverridePath;
        lookupPath = gLookupOverridePath;
        gBankSourcesDirty = false;
    }
    ReleaseSRWLockExclusive(&gOverrideLock);
    return changed;
}

bool TakeDynamicBankUpdate() {
    AcquireSRWLockExclusive(&gOverrideLock);
    const bool changed = gDynamicBanksDirty;
    gDynamicBanksDirty = false;
    ReleaseSRWLockExclusive(&gOverrideLock);
    return changed;
}

void PublishCompletion(const Voice& voice) {
    if (!voice.reportsCompletion || voice.vehicleSourceKey == 0) {
        return;
    }
    QueueCompletion({
        voice.vehicleSourceKey,
        voice.vehicleGeneration
    });
}

void PublishDialogueStarted(
    const AudioJob& job,
    const OriginalPcmSample& sample
) {
    const auto samples = sample.pcm.size() / sizeof(std::int16_t);
    const auto length = sample.sampleRate != 0
        ? static_cast<double>(samples) * 1000.0 /
            static_cast<double>(sample.sampleRate) /
            std::max(static_cast<double>(job.baseSpeed), 0.05)
        : 0.0;
    QueueCompletion({
        job.sourceKey,
        job.sourceGeneration,
        false,
        static_cast<std::int16_t>(std::clamp(length, 0.0, 32767.0))
    });
}

void PublishVehiclePlayTimeMetadata(
    const AudioJob& job,
    const OriginalPcmSample& sample
) {
    if (!job.reportPlayTime || sample.sampleRate == 0) {
        return;
    }
    const auto samples = sample.pcm.size() / sizeof(std::int16_t);
    const auto length =
        static_cast<double>(samples) * 1000.0 /
        static_cast<double>(sample.sampleRate);
    QueueCompletion({
        job.sourceKey,
        job.sourceGeneration,
        false,
        static_cast<std::int16_t>(std::clamp(length, 1.0, 32767.0))
    });
}

void PublishCompletion(const AudioJob& job) {
    Voice voice{};
    voice.reportsCompletion = true;
    voice.vehicleSourceKey = job.sourceKey;
    voice.vehicleGeneration = job.sourceGeneration;
    PublishCompletion(voice);
}

} // namespace backend
