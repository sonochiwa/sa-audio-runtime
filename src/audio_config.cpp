#include "audio_config.h"

#include "resource.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

char gPath[MAX_PATH]{};
std::atomic<bool> gEnabled{true};
std::atomic<bool> gHotkeyEnabled{true};
std::atomic<int> gHotkeyKey{0};
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

HMODULE gModule{};

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
    if (legacyKey && HasValue("modules", legacyKey)) {
        return ReadBoolean("modules", legacyKey, defaultValue);
    }
    return defaultValue;
}

bool ReadVehicleLegacyValue(bool defaultValue) {
    if (HasValue("modules", "vehicles")) {
        return ReadBoolean("modules", "vehicles", defaultValue);
    }
    if (HasValue("modules", "vehicleEngines")) {
        return ReadBoolean("modules", "vehicleEngines", defaultValue);
    }
    return defaultValue;
}

bool ReadMiscLegacyValue(bool defaultValue) {
    if (HasValue("modules", "oneShotEffects")) {
        return ReadBoolean("modules", "oneShotEffects", defaultValue);
    }
    if (HasValue("modules", "effects")) {
        return ReadBoolean("modules", "effects", defaultValue);
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
    const bool hotkeyEnabled = ReadBoolean(
        "general",
        "hotkeyEnabled",
        true
    );
    const int hotkeyKey = std::clamp(
        static_cast<int>(GetPrivateProfileIntA(
            "general",
            "hotkeyKey",
            0,
            gPath
        )),
        0,
        255
    );
    const int hotkeyModifier = std::clamp(
        static_cast<int>(GetPrivateProfileIntA(
            "general",
            "hotkeyModifier",
            VK_MENU,
            gPath
        )),
        0,
        255
    );
    const bool showNotifications = ReadBoolean(
        "general",
        "showNotifications",
        true
    );

    const bool gunshotsEnabled = ReadMigratedBoolean(
        "weaponAudio",
        "gunshots",
        "gunshots",
        true
    );
    const bool bulletImpactsEnabled = ReadMigratedBoolean(
        "weaponAudio",
        "bulletImpacts",
        "gunshots",
        true
    );
    const bool weaponEffectsEnabled = ReadMigratedBoolean(
        "weaponAudio",
        "effects",
        "weaponEffects",
        true
    );

    const bool legacyVehicles = ReadVehicleLegacyValue(true);
    const bool vehicleEnginesEnabled = HasValue(
        "vehicleAudio",
        "engines"
    ) ? ReadBoolean("vehicleAudio", "engines", true) : legacyVehicles;
    const bool vehicleEffectsEnabled = HasValue(
        "vehicleAudio",
        "effects"
    ) ? ReadBoolean("vehicleAudio", "effects", true) : legacyVehicles;
    const bool vehicleCollisionsEnabled = ReadMigratedBoolean(
        "vehicleAudio",
        "collisions",
        "vehicleCollisions",
        true
    );

    const bool dialoguesEnabled = ReadMigratedBoolean(
        "characterAudio",
        "dialogues",
        "dialogues",
        true
    );
    const bool scannerEnabled = ReadMigratedBoolean(
        "characterAudio",
        "scanner",
        "scanner",
        true
    );
    const bool characterEffectsEnabled = ReadMigratedBoolean(
        "characterAudio",
        "effects",
        "characters",
        true
    );

    const bool explosionsEnabled = ReadMigratedBoolean(
        "worldAudio",
        "explosions",
        "explosions",
        true
    );
    const bool worldAmbienceEnabled = ReadMigratedBoolean(
        "worldAudio",
        "ambience",
        "worldAmbience",
        true
    );
    const bool miscEffectsEnabled = HasValue(
        "worldAudio",
        "miscEffects"
    ) ? ReadBoolean("worldAudio", "miscEffects", true)
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

    WriteBoolean("general", "isEnabled", enabled);
    WriteBoolean("general", "hotkeyEnabled", hotkeyEnabled);
    WriteInteger("general", "hotkeyModifier", hotkeyModifier);
    WriteInteger("general", "hotkeyKey", hotkeyKey);
    WriteBoolean(
        "general",
        "showNotifications",
        showNotifications
    );

    WriteBoolean("weaponAudio", "gunshots", gunshotsEnabled);
    WriteBoolean(
        "weaponAudio",
        "bulletImpacts",
        bulletImpactsEnabled
    );
    WriteBoolean("weaponAudio", "effects", weaponEffectsEnabled);
    WriteBoolean("vehicleAudio", "engines", vehicleEnginesEnabled);
    WriteBoolean("vehicleAudio", "effects", vehicleEffectsEnabled);
    WriteBoolean(
        "vehicleAudio",
        "collisions",
        vehicleCollisionsEnabled
    );
    WriteBoolean("characterAudio", "dialogues", dialoguesEnabled);
    WriteBoolean("characterAudio", "scanner", scannerEnabled);
    WriteBoolean(
        "characterAudio",
        "effects",
        characterEffectsEnabled
    );
    WriteBoolean("worldAudio", "explosions", explosionsEnabled);
    WriteBoolean("worldAudio", "ambience", worldAmbienceEnabled);
    WriteBoolean("worldAudio", "miscEffects", miscEffectsEnabled);

    WritePrivateProfileStringA("modules", nullptr, nullptr, gPath);
    WritePrivateProfileStringA("general", "ReloadCommand", nullptr, gPath);
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
