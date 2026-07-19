#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {

struct modloader_t;
struct modloader_plugin_t;

struct modloader_mod_t {
    std::uint64_t id;
    std::uint32_t priority;
    std::uint32_t reserved;
};

struct modloader_file_t {
    std::uint32_t flags;
    const char* buffer;
    std::uint8_t pos_eos;
    std::uint8_t pos_filedir;
    std::uint8_t pos_filename;
    std::uint8_t pos_filext;
    std::uint32_t hash;
    std::uint32_t reserved;
    modloader_mod_t* parent;
    std::uint64_t size;
    std::uint64_t time;
    std::uint64_t behaviour;
};

struct modloader_t {
    const char* gamepath;
    const char* reservedPath;
    const char* commonappdata;
    const char* localappdata;
    const char* reservedPointers[2];
    std::uint32_t reservedValues[4];
    std::uint8_t has_game_started;
    std::uint8_t has_game_loaded;
    std::uint8_t reservedBytes[2];
    void* Log;
    void* vLog;
    void* Error;
    void* CreateSharedData;
    void* DeleteSharedData;
    void* FindSharedData;
};

struct modloader_plugin_t {
    std::uint8_t major;
    std::uint8_t minor;
    std::uint8_t revision;
    std::uint8_t pad0;
    void* pThis;
    void* pModule;
    const char* name;
    const char* author;
    const char* version;
    modloader_t* loader;
    std::uint8_t has_started;
    std::uint8_t pad1[3];
    void* userdata;
    const char** extable;
    std::size_t extable_len;
    int priority;
    const char* (__cdecl* GetAuthor)(modloader_plugin_t*);
    const char* (__cdecl* GetVersion)(modloader_plugin_t*);
    int (__cdecl* OnStartup)(modloader_plugin_t*);
    int (__cdecl* OnShutdown)(modloader_plugin_t*);
    int (__cdecl* GetBehaviour)(modloader_plugin_t*, modloader_file_t*);
    int (__cdecl* InstallFile)(modloader_plugin_t*, const modloader_file_t*);
    int (__cdecl* ReinstallFile)(modloader_plugin_t*, const modloader_file_t*);
    int (__cdecl* UninstallFile)(modloader_plugin_t*, const modloader_file_t*);
    void (__cdecl* Update)(modloader_plugin_t*);
};

}

namespace {

using SampleCallback = void(__cdecl*)(std::int32_t, const char*, std::int32_t);
constexpr char kPluginVersion[] = "1.0.0";
constexpr int kWeaponLocalBank = 137;
constexpr std::size_t kMaxSounds = 400;
modloader_t* gLoader{};
std::array<std::string, kMaxSounds> gPaths{};
std::array<bool, kMaxSounds> gInstalled{};

SampleCallback FindBackend() {
    const auto module = GetModuleHandleA("AudioRuntime.asi");
    if (!module) {
        return nullptr;
    }
    auto callback = reinterpret_cast<SampleCallback>(
        GetProcAddress(
            module,
            "AudioRuntimeModLoaderSample"
        )
    );
    if (!callback) {
        callback = reinterpret_cast<SampleCallback>(
            GetProcAddress(
                module,
                "_AudioRuntimeModLoaderSample"
            )
        );
    }
    return callback;
}

int GetWeaponSoundId(const modloader_file_t* file) {
    if (!file || !file->buffer) {
        return -1;
    }
    std::string path(file->buffer);
    for (auto& character : path) {
        if (character == '/') {
            character = '\\';
        } else if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    int bank{-1};
    int sound{-1};
    const auto marker = path.rfind("\\genrl\\bank_");
    const char* target = marker == std::string::npos
        ? path.c_str()
        : path.c_str() + marker + 1;
    if (std::sscanf(
            target,
            "genrl\\bank_%d\\sound_%d.wav",
            &bank,
            &sound
        ) != 2 ||
        bank != kWeaponLocalBank ||
        sound < 1 ||
        sound > static_cast<int>(kMaxSounds)) {
        return -1;
    }
    // ModLoader WAV names are one-based; CAE sound IDs are zero-based.
    return sound - 1;
}

void Deliver(int soundId) {
    const auto callback = FindBackend();
    if (!callback || soundId < 0 ||
        soundId >= static_cast<int>(kMaxSounds)) {
        return;
    }
    callback(
        soundId,
        gPaths[static_cast<std::size_t>(soundId)].c_str(),
        gInstalled[static_cast<std::size_t>(soundId)] ? 1 : 0
    );
}

int StoreFile(const modloader_file_t* file, bool installed) {
    const auto soundId = GetWeaponSoundId(file);
    if (soundId < 0) {
        return 0;
    }
    const auto index = static_cast<std::size_t>(soundId);
    gInstalled[index] = installed;
    if (installed && gLoader && gLoader->gamepath) {
        gPaths[index] = gLoader->gamepath;
        gPaths[index] += file->buffer;
    } else {
        gPaths[index].clear();
    }
    Deliver(soundId);
    return 0;
}

const char* __cdecl GetAuthor(modloader_plugin_t*) {
    return "sonochiwa";
}

const char* __cdecl GetVersion(modloader_plugin_t*) {
    return kPluginVersion;
}

int __cdecl OnStartup(modloader_plugin_t* plugin) {
    gLoader = plugin->loader;
    return 0;
}

int __cdecl OnShutdown(modloader_plugin_t*) {
    return 0;
}

int __cdecl GetBehaviour(
    modloader_plugin_t*,
    modloader_file_t* file
) {
    return GetWeaponSoundId(file) >= 0 ? 2 : 0;
}

int __cdecl InstallFile(
    modloader_plugin_t*,
    const modloader_file_t* file
) {
    return StoreFile(file, true);
}

int __cdecl ReinstallFile(
    modloader_plugin_t*,
    const modloader_file_t* file
) {
    return StoreFile(file, true);
}

int __cdecl UninstallFile(
    modloader_plugin_t*,
    const modloader_file_t* file
) {
    return StoreFile(file, false);
}

void __cdecl Update(modloader_plugin_t*) {
    if (!FindBackend()) {
        return;
    }
    for (std::size_t index = 0; index < kMaxSounds; ++index) {
        if (gInstalled[index]) {
            Deliver(static_cast<int>(index));
        }
    }
}

const char* gExtensions[] = {"wav"};

} // namespace

extern "C" __declspec(dllexport) void GetLoaderVersion(
    std::uint8_t* major,
    std::uint8_t* minor,
    std::uint8_t* revision
) {
    *major = 0;
    *minor = 3;
    *revision = 7;
}

extern "C" __declspec(dllexport) void GetPluginData(
    modloader_plugin_t* plugin
) {
    plugin->name = "SA Audio Runtime ModLoader Bridge";
    plugin->author = "sonochiwa";
    plugin->version = kPluginVersion;
    plugin->extable = gExtensions;
    plugin->extable_len = 1;
    // Must run before gta3.std.bank (priority 50).
    plugin->priority = 1;
    plugin->GetAuthor = &GetAuthor;
    plugin->GetVersion = &GetVersion;
    plugin->OnStartup = &OnStartup;
    plugin->OnShutdown = &OnShutdown;
    plugin->GetBehaviour = &GetBehaviour;
    plugin->InstallFile = &InstallFile;
    plugin->ReinstallFile = &ReinstallFile;
    plugin->UninstallFile = &UninstallFile;
    plugin->Update = &Update;
}

extern "C" __declspec(dllexport) void
AudioRuntimeReplayModLoaderSamples() {
    for (std::size_t index = 0; index < kMaxSounds; ++index) {
        if (gInstalled[index]) {
            Deliver(static_cast<int>(index));
        }
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, void*) {
    return TRUE;
}
