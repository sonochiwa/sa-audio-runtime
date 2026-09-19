#include "audio_config.h"

#include "cheat_command.h"

#include "resource.h"

#include <initializer_list>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

char gPath[MAX_PATH]{};
std::atomic<bool> gEnabled{true};

HMODULE gModule{};

std::string GetModuleDirectory(HMODULE module) {
    char path[MAX_PATH]{};
    GetModuleFileNameA(module, path, MAX_PATH);
    if (auto* slash = std::strrchr(path, '\\')) {
        *slash = '\0';
    }
    return path;
}

bool ReadBoolean(
    const char* section,
    const char* key,
    bool defaultValue
) {
    return GetPrivateProfileIntA(
        section,
        key,
        defaultValue ? 1 : 0,
        gPath
    ) != 0;
}

void WriteInteger(const char* section, const char* key, int value) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%d", value);
    WritePrivateProfileStringA(section, key, text, gPath);
}

void WriteBoolean(const char* section, const char* key, bool value) {
    WriteInteger(section, key, value ? 1 : 0);
}

// Writes the RCDATA copy of Config\AudioRuntime.ini byte for byte. A
// master switch migrated from an older configuration is written afterwards
// through the profile API, so only a migrated file differs from the
// canonical one.
void CreateDefaultConfiguration(bool enabled) {
    const HRSRC resource = FindResourceW(gModule, MAKEINTRESOURCEW(IDR_DEFAULT_INI), RT_RCDATA);
    if (!resource) {
        return;
    }
    const HGLOBAL handle = LoadResource(gModule, resource);
    const DWORD size = SizeofResource(gModule, resource);
    const void* data = handle ? LockResource(handle) : nullptr;
    if (!data || size == 0) {
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
    WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);

    if (!enabled) {
        WriteBoolean("general", "isEnabled", false);
    }
}

void ReadConfiguration(bool migratedEnabled) {
    const bool enabled = ReadBoolean(
        "general",
        "isEnabled",
        migratedEnabled
    );
    // A missing key keeps the compiled default; a present empty one
    // disables the command rather than falling back to it.
    char command[32]{};
    GetPrivateProfileStringA(
        "general",
        "command",
        "AUDIORUNTIME",
        command,
        sizeof(command),
        gPath
    );
    gEnabled.store(enabled, std::memory_order_release);
    CheatCommandSetWord(command);
    WriteBoolean("general", "isEnabled", enabled);
    WritePrivateProfileStringA("general", "command", command, gPath);
    for (const char* section : {"modules", "weaponAudio", "vehicleAudio", "characterAudio", "worldAudio"}) {
        WritePrivateProfileStringA(section, nullptr, nullptr, gPath);
    }
    for (const char* key : {"ReloadCommand", "showNotifications", "hotkeyEnabled", "hotkeyModifier", "hotkeyKey"}) {
        WritePrivateProfileStringA("general", key, nullptr, gPath);
    }
}

} // namespace

void AudioConfigInitialize(HMODULE module) {
    gModule = module;
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
                    "modules",
                    "gunshots",
                    GetPrivateProfileIntA(
                        "general",
                        "isEnabled",
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
    WriteBoolean("general", "isEnabled", enabled);
}
