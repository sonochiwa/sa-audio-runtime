#include "backend/backend.h"

namespace backend {

bool InitialiseListener(
    IDirectSound8* directSound,
    IDirectSound3DListener** output
) {
    DSBUFFERDESC description{};
    description.dwSize = sizeof(description);
    description.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_PRIMARYBUFFER;

    IDirectSoundBuffer* primary{};
    if (FAILED(directSound->CreateSoundBuffer(
            &description,
            &primary,
            nullptr
        ))) {
        return false;
    }

    IDirectSound3DListener* listener{};
    const auto result = primary->QueryInterface(
        IID_IDirectSound3DListener,
        reinterpret_cast<void**>(&listener)
    );
    primary->Release();
    if (FAILED(result) || !listener) {
        return false;
    }

    if (FAILED(listener->SetPosition(
            0.0f,
            0.0f,
            0.0f,
            DS3D_IMMEDIATE
        )) ||
        FAILED(listener->SetOrientation(
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            -1.0f,
            DS3D_IMMEDIATE
        )) ||
        FAILED(listener->SetRolloffFactor(0.0f, DS3D_IMMEDIATE)) ||
        FAILED(listener->SetDopplerFactor(0.0f, DS3D_IMMEDIATE))) {
        listener->Release();
        return false;
    }
    *output = listener;
    return true;
}

bool CreateAudioDevice(
    IDirectSound8** directSoundOutput,
    IDirectSound3DListener** listenerOutput
) {
    auto* window = FindProcessWindow();
    if (!window) {
        return false;
    }

    IDirectSound8* directSound{};
    if (FAILED(DirectSoundCreate8(nullptr, &directSound, nullptr))) {
        return false;
    }
    if (FAILED(directSound->SetCooperativeLevel(
            window,
            DSSCL_NORMAL
        ))) {
        directSound->Release();
        return false;
    }

    IDirectSound3DListener* listener{};
    if (!InitialiseListener(directSound, &listener)) {
        directSound->Release();
        return false;
    }

    *directSoundOutput = directSound;
    *listenerOutput = listener;
    return true;
}

bool IsAudioDeviceHealthy(IDirectSound8* directSound) {
    if (!directSound) {
        return false;
    }
    DSCAPS capabilities{};
    capabilities.dwSize = sizeof(capabilities);
    return SUCCEEDED(directSound->GetCaps(&capabilities));
}

void ReleaseBaseBuffers(
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& buffers
) {
    for (auto*& buffer : buffers) {
        if (buffer) {
            buffer->Release();
            buffer = nullptr;
        }
    }
}

} // namespace backend
