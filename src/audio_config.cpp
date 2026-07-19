#include "audio_config.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

char gPath[MAX_PATH]{};
std::atomic<bool> gEnabled{true};
std::atomic<bool> gHotkeyEnabled{true};
std::atomic<int> gHotkeyKey{'Y'};
std::atomic<int> gHotkeyModifier{VK_MENU};
std::atomic<bool> gShowNotifications{true};
std::atomic<bool> gGunshotsEnabled{true};

std::string GetModuleDirectory(HMODULE module) {
    char path[MAX_PATH]{};
    GetModuleFileNameA(module, path, MAX_PATH);
    if (auto* slash = std::strrchr(path, '\\')) {
        *slash = '\0';
    }
    return path;
}

void WriteInteger(const char* section, const char* key, int value) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%d", value);
    WritePrivateProfileStringA(section, key, text, gPath);
}

void CreateDefaultConfiguration(bool enabled) {
    char text[512]{};
    const int length = std::snprintf(
        text,
        sizeof(text),
        "# SA Audio Runtime v1.0.0\r\n"
        "# Created by sonochiwa\r\n"
        "# Source code: https://github.com/sonochiwa/sa-audio-runtime\r\n"
        "# Default toggle hotkey: Alt + Y\r\n"
        "\r\n"
        "[General]\r\n"
        "IsEnabled=%d\r\n"
        "HotkeyEnabled=1\r\n"
        "HotkeyModifier=18\r\n"
        "HotkeyKey=89\r\n"
        "ShowNotifications=1\r\n"
        "\r\n"
        "[Modules]\r\n"
        "gunshots=1\r\n",
        enabled ? 1 : 0
    );
    if (length <= 0 || length >= static_cast<int>(sizeof(text))) {
        return;
    }

    const HANDLE file = CreateFileA(
        gPath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written{};
    WriteFile(
        file,
        text,
        static_cast<DWORD>(length),
        &written,
        nullptr
    );
    CloseHandle(file);
}

void ReadConfiguration(bool migratedEnabled) {
    const bool enabled =
        GetPrivateProfileIntA(
            "General",
            "IsEnabled",
            migratedEnabled ? 1 : 0,
            gPath
        ) != 0;
    const bool hotkeyEnabled =
        GetPrivateProfileIntA(
            "General",
            "HotkeyEnabled",
            1,
            gPath
        ) != 0;
    const int hotkeyKey = std::clamp(
        static_cast<int>(
            GetPrivateProfileIntA(
                "General",
                "HotkeyKey",
                'Y',
                gPath
            )
        ),
        0,
        255
    );
    const int hotkeyModifier = std::clamp(
        static_cast<int>(
            GetPrivateProfileIntA(
                "General",
                "HotkeyModifier",
                VK_MENU,
                gPath
            )
        ),
        0,
        255
    );
    const bool showNotifications =
        GetPrivateProfileIntA(
            "General",
            "ShowNotifications",
            1,
            gPath
        ) != 0;
    const bool gunshotsEnabled =
        GetPrivateProfileIntA(
            "Modules",
            "gunshots",
            1,
            gPath
        ) != 0;

    gEnabled.store(enabled, std::memory_order_release);
    gHotkeyEnabled.store(hotkeyEnabled, std::memory_order_release);
    gHotkeyKey.store(hotkeyKey, std::memory_order_release);
    gHotkeyModifier.store(hotkeyModifier, std::memory_order_release);
    gShowNotifications.store(showNotifications, std::memory_order_release);
    gGunshotsEnabled.store(gunshotsEnabled, std::memory_order_release);

    WriteInteger("General", "IsEnabled", enabled ? 1 : 0);
    WriteInteger("General", "HotkeyEnabled", hotkeyEnabled ? 1 : 0);
    WriteInteger("General", "HotkeyKey", hotkeyKey);
    WriteInteger("General", "HotkeyModifier", hotkeyModifier);
    WriteInteger(
        "General",
        "ShowNotifications",
        showNotifications ? 1 : 0
    );
    WriteInteger("Modules", "gunshots", gunshotsEnabled ? 1 : 0);

    WritePrivateProfileStringA("General", "ReloadCommand", nullptr, gPath);
    constexpr const char* kRemovedModules[] = {
        "WeaponEffects",
        "VehicleEngines",
        "VehicleEffects",
        "WorldAmbience",
        "CharacterEffects",
        "Frontend",
        "Streams"
    };
    for (const auto* key : kRemovedModules) {
        WritePrivateProfileStringA("Modules", key, nullptr, gPath);
    }
}

} // namespace

void AudioConfigInitialize(HMODULE module) {
    const auto directory = GetModuleDirectory(module);
    std::snprintf(
        gPath,
        sizeof(gPath),
        "%s\\AudioRuntime.ini",
        directory.c_str()
    );

    bool migratedEnabled = true;
    const bool configMissing =
        GetFileAttributesA(gPath) == INVALID_FILE_ATTRIBUTES;
    if (configMissing) {
        auto oldPath = directory + "\\AudioOffload.ini";
        if (GetFileAttributesA(oldPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            oldPath = directory + "\\GunshotAudioOptimizer.ini";
        }
        if (GetFileAttributesA(oldPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            migratedEnabled =
                GetPrivateProfileIntA(
                    "Modules",
                    "gunshots",
                    GetPrivateProfileIntA(
                        "General",
                        "IsEnabled",
                        1,
                        oldPath.c_str()
                    ),
                    oldPath.c_str()
                ) != 0;
        }
        CreateDefaultConfiguration(migratedEnabled);
    }

    ReadConfiguration(migratedEnabled);
}

void AudioConfigReload() {
    ReadConfiguration(true);
}

bool AudioConfigIsEnabled() {
    return gEnabled.load(std::memory_order_acquire);
}

void AudioConfigSetEnabled(bool enabled) {
    gEnabled.store(enabled, std::memory_order_release);
    WriteInteger("General", "IsEnabled", enabled ? 1 : 0);
}

bool AudioConfigHotkeyEnabled() {
    return gHotkeyEnabled.load(std::memory_order_acquire);
}

int AudioConfigHotkeyKey() {
    return gHotkeyKey.load(std::memory_order_acquire);
}

int AudioConfigHotkeyModifier() {
    return gHotkeyModifier.load(std::memory_order_acquire);
}

bool AudioConfigShowNotifications() {
    return gShowNotifications.load(std::memory_order_acquire);
}

bool AudioConfigGunshotsEnabled() {
    return gGunshotsEnabled.load(std::memory_order_acquire);
}
