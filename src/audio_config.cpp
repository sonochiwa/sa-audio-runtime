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
std::atomic<bool> gBulletImpactsEnabled{true};
std::atomic<bool> gWeaponEffectsEnabled{true};
std::atomic<bool> gVehicleEnginesEnabled{true};
std::atomic<bool> gVehicleEffectsEnabled{true};
std::atomic<bool> gVehicleCollisionsEnabled{true};
std::atomic<bool> gDialoguesEnabled{true};
std::atomic<bool> gScannerEnabled{true};
std::atomic<bool> gCharacterEffectsEnabled{true};
std::atomic<bool> gExplosionsEnabled{true};
std::atomic<bool> gWorldAmbienceEnabled{true};
std::atomic<bool> gMiscEffectsEnabled{true};

std::string GetModuleDirectory(HMODULE module) {
    char path[MAX_PATH]{};
    GetModuleFileNameA(module, path, MAX_PATH);
    if (auto* slash = std::strrchr(path, '\\')) {
        *slash = '\0';
    }
    return path;
}

bool HasValue(const char* section, const char* key) {
    char value[16]{};
    return GetPrivateProfileStringA(
        section,
        key,
        "",
        value,
        sizeof(value),
        gPath
    ) != 0;
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

bool ReadMigratedBoolean(
    const char* section,
    const char* key,
    const char* legacyKey,
    bool defaultValue
) {
    if (HasValue(section, key)) {
        return ReadBoolean(section, key, defaultValue);
    }
    if (legacyKey && HasValue("Modules", legacyKey)) {
        return ReadBoolean("Modules", legacyKey, defaultValue);
    }
    return defaultValue;
}

bool ReadVehicleLegacyValue(bool defaultValue) {
    if (HasValue("Modules", "vehicles")) {
        return ReadBoolean("Modules", "vehicles", defaultValue);
    }
    if (HasValue("Modules", "vehicleEngines")) {
        return ReadBoolean("Modules", "vehicleEngines", defaultValue);
    }
    return defaultValue;
}

bool ReadMiscLegacyValue(bool defaultValue) {
    if (HasValue("Modules", "oneShotEffects")) {
        return ReadBoolean("Modules", "oneShotEffects", defaultValue);
    }
    if (HasValue("Modules", "effects")) {
        return ReadBoolean("Modules", "effects", defaultValue);
    }
    return defaultValue;
}

void WriteInteger(const char* section, const char* key, int value) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%d", value);
    WritePrivateProfileStringA(section, key, text, gPath);
}

void WriteBoolean(const char* section, const char* key, bool value) {
    WriteInteger(section, key, value ? 1 : 0);
}

void CreateDefaultConfiguration(bool enabled) {
    char text[1024]{};
    const int length = std::snprintf(
        text,
        sizeof(text),
        "# SA Audio Runtime v2.0.0\r\n"
        "# Created by sonochiwa\r\n"
        "# Source code: https://github.com/sonochiwa/sa-audio-runtime\r\n"
        "# Default toggle hotkey: Alt + Y\r\n"
        "\r\n"
        "[General]\r\n"
        "isEnabled=%d\r\n"
        "hotkeyEnabled=1\r\n"
        "hotkeyModifier=18\r\n"
        "hotkeyKey=89\r\n"
        "showNotifications=1\r\n"
        "\r\n"
        "[WeaponAudio]\r\n"
        "gunshots=1\r\n"
        "bulletImpacts=1\r\n"
        "effects=1\r\n"
        "\r\n"
        "[VehicleAudio]\r\n"
        "engines=1\r\n"
        "effects=1\r\n"
        "collisions=1\r\n"
        "\r\n"
        "[CharacterAudio]\r\n"
        "dialogues=1\r\n"
        "scanner=1\r\n"
        "effects=1\r\n"
        "\r\n"
        "[WorldAudio]\r\n"
        "explosions=1\r\n"
        "ambience=1\r\n"
        "miscEffects=1\r\n",
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
    const bool enabled = ReadBoolean(
        "General",
        "isEnabled",
        migratedEnabled
    );
    const bool hotkeyEnabled = ReadBoolean(
        "General",
        "hotkeyEnabled",
        true
    );
    const int hotkeyKey = std::clamp(
        static_cast<int>(GetPrivateProfileIntA(
            "General",
            "hotkeyKey",
            'Y',
            gPath
        )),
        0,
        255
    );
    const int hotkeyModifier = std::clamp(
        static_cast<int>(GetPrivateProfileIntA(
            "General",
            "hotkeyModifier",
            VK_MENU,
            gPath
        )),
        0,
        255
    );
    const bool showNotifications = ReadBoolean(
        "General",
        "showNotifications",
        true
    );

    const bool gunshotsEnabled = ReadMigratedBoolean(
        "WeaponAudio",
        "gunshots",
        "gunshots",
        true
    );
    const bool bulletImpactsEnabled = ReadMigratedBoolean(
        "WeaponAudio",
        "bulletImpacts",
        "gunshots",
        true
    );
    const bool weaponEffectsEnabled = ReadMigratedBoolean(
        "WeaponAudio",
        "effects",
        "weaponEffects",
        true
    );

    const bool legacyVehicles = ReadVehicleLegacyValue(true);
    const bool vehicleEnginesEnabled = HasValue(
        "VehicleAudio",
        "engines"
    ) ? ReadBoolean("VehicleAudio", "engines", true) : legacyVehicles;
    const bool vehicleEffectsEnabled = HasValue(
        "VehicleAudio",
        "effects"
    ) ? ReadBoolean("VehicleAudio", "effects", true) : legacyVehicles;
    const bool vehicleCollisionsEnabled = ReadMigratedBoolean(
        "VehicleAudio",
        "collisions",
        "vehicleCollisions",
        true
    );

    const bool dialoguesEnabled = ReadMigratedBoolean(
        "CharacterAudio",
        "dialogues",
        "dialogues",
        true
    );
    const bool scannerEnabled = ReadMigratedBoolean(
        "CharacterAudio",
        "scanner",
        "scanner",
        true
    );
    const bool characterEffectsEnabled = ReadMigratedBoolean(
        "CharacterAudio",
        "effects",
        "characters",
        true
    );

    const bool explosionsEnabled = ReadMigratedBoolean(
        "WorldAudio",
        "explosions",
        "explosions",
        true
    );
    const bool worldAmbienceEnabled = ReadMigratedBoolean(
        "WorldAudio",
        "ambience",
        "worldAmbience",
        true
    );
    const bool miscEffectsEnabled = HasValue(
        "WorldAudio",
        "miscEffects"
    ) ? ReadBoolean("WorldAudio", "miscEffects", true)
      : ReadMiscLegacyValue(true);

    gEnabled.store(enabled, std::memory_order_release);
    gHotkeyEnabled.store(hotkeyEnabled, std::memory_order_release);
    gHotkeyKey.store(hotkeyKey, std::memory_order_release);
    gHotkeyModifier.store(hotkeyModifier, std::memory_order_release);
    gShowNotifications.store(showNotifications, std::memory_order_release);
    gGunshotsEnabled.store(gunshotsEnabled, std::memory_order_release);
    gBulletImpactsEnabled.store(
        bulletImpactsEnabled,
        std::memory_order_release
    );
    gWeaponEffectsEnabled.store(
        weaponEffectsEnabled,
        std::memory_order_release
    );
    gVehicleEnginesEnabled.store(
        vehicleEnginesEnabled,
        std::memory_order_release
    );
    gVehicleEffectsEnabled.store(
        vehicleEffectsEnabled,
        std::memory_order_release
    );
    gVehicleCollisionsEnabled.store(
        vehicleCollisionsEnabled,
        std::memory_order_release
    );
    gDialoguesEnabled.store(dialoguesEnabled, std::memory_order_release);
    gScannerEnabled.store(scannerEnabled, std::memory_order_release);
    gCharacterEffectsEnabled.store(
        characterEffectsEnabled,
        std::memory_order_release
    );
    gExplosionsEnabled.store(explosionsEnabled, std::memory_order_release);
    gWorldAmbienceEnabled.store(
        worldAmbienceEnabled,
        std::memory_order_release
    );
    gMiscEffectsEnabled.store(
        miscEffectsEnabled,
        std::memory_order_release
    );

    WriteBoolean("General", "isEnabled", enabled);
    WriteBoolean("General", "hotkeyEnabled", hotkeyEnabled);
    WriteInteger("General", "hotkeyModifier", hotkeyModifier);
    WriteInteger("General", "hotkeyKey", hotkeyKey);
    WriteBoolean(
        "General",
        "showNotifications",
        showNotifications
    );

    WriteBoolean("WeaponAudio", "gunshots", gunshotsEnabled);
    WriteBoolean(
        "WeaponAudio",
        "bulletImpacts",
        bulletImpactsEnabled
    );
    WriteBoolean("WeaponAudio", "effects", weaponEffectsEnabled);
    WriteBoolean("VehicleAudio", "engines", vehicleEnginesEnabled);
    WriteBoolean("VehicleAudio", "effects", vehicleEffectsEnabled);
    WriteBoolean(
        "VehicleAudio",
        "collisions",
        vehicleCollisionsEnabled
    );
    WriteBoolean("CharacterAudio", "dialogues", dialoguesEnabled);
    WriteBoolean("CharacterAudio", "scanner", scannerEnabled);
    WriteBoolean(
        "CharacterAudio",
        "effects",
        characterEffectsEnabled
    );
    WriteBoolean("WorldAudio", "explosions", explosionsEnabled);
    WriteBoolean("WorldAudio", "ambience", worldAmbienceEnabled);
    WriteBoolean("WorldAudio", "miscEffects", miscEffectsEnabled);

    WritePrivateProfileStringA("Modules", nullptr, nullptr, gPath);
    WritePrivateProfileStringA("General", "ReloadCommand", nullptr, gPath);
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
    WriteBoolean("General", "isEnabled", enabled);
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

bool AudioConfigBulletImpactsEnabled() {
    return gBulletImpactsEnabled.load(std::memory_order_acquire);
}

bool AudioConfigWeaponEffectsEnabled() {
    return gWeaponEffectsEnabled.load(std::memory_order_acquire);
}

bool AudioConfigVehicleEnginesEnabled() {
    return gVehicleEnginesEnabled.load(std::memory_order_acquire);
}

bool AudioConfigVehicleEffectsEnabled() {
    return gVehicleEffectsEnabled.load(std::memory_order_acquire);
}

bool AudioConfigVehicleCollisionsEnabled() {
    return gVehicleCollisionsEnabled.load(std::memory_order_acquire);
}

bool AudioConfigDialoguesEnabled() {
    return gDialoguesEnabled.load(std::memory_order_acquire);
}

bool AudioConfigScannerEnabled() {
    return gScannerEnabled.load(std::memory_order_acquire);
}

bool AudioConfigCharacterEffectsEnabled() {
    return gCharacterEffectsEnabled.load(std::memory_order_acquire);
}

bool AudioConfigExplosionsEnabled() {
    return gExplosionsEnabled.load(std::memory_order_acquire);
}

bool AudioConfigWorldAmbienceEnabled() {
    return gWorldAmbienceEnabled.load(std::memory_order_acquire);
}

bool AudioConfigMiscEffectsEnabled() {
    return gMiscEffectsEnabled.load(std::memory_order_acquire);
}
