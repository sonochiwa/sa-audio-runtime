#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
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

using SampleCallback = void(__cdecl*)(
    std::int32_t,
    std::int32_t,
    const char*,
    std::int32_t
);
using SourcesCallback = void(__cdecl*)(const char*, const char*);
constexpr char kPluginVersion[] = "1.1.0";
constexpr int kWeaponLocalBank = 137;
constexpr int kBulletHitLocalBank = 21;
constexpr std::size_t kRuntimeBankCount = 2;
constexpr std::size_t kMaxSounds = 400;
modloader_t* gLoader{};
std::array<
    std::array<std::string, kMaxSounds>,
    kRuntimeBankCount
> gPaths{};
std::array<
    std::array<bool, kMaxSounds>,
    kRuntimeBankCount
> gInstalled{};
std::string gArchivePath;
std::string gLookupPath;
bool gArchiveInstalled{};
bool gLookupInstalled{};

struct SoundReference {
    int bank{-1};
    int sound{-1};
};

HMODULE FindBackendModule() {
    return GetModuleHandleA("AudioRuntime.asi");
}

SampleCallback FindBackend() {
    const auto module = FindBackendModule();
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

SourcesCallback FindSourcesBackend() {
    const auto module = FindBackendModule();
    if (!module) {
        return nullptr;
    }
    auto callback = reinterpret_cast<SourcesCallback>(
        GetProcAddress(module, "AudioRuntimeModLoaderSources")
    );
    if (!callback) {
        callback = reinterpret_cast<SourcesCallback>(
            GetProcAddress(module, "_AudioRuntimeModLoaderSources")
        );
    }
    return callback;
}

std::string NormalisePath(const modloader_file_t* file) {
    if (!file || !file->buffer) {
        return {};
    }
    std::string path(file->buffer);
    for (auto& character : path) {
        if (character == '/') {
            character = '\\';
        } else if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return path;
}

SoundReference GetSoundReference(const modloader_file_t* file) {
    if (!file || !file->buffer) {
        return {};
    }
    const auto path = NormalisePath(file);
    int bank{-1};
    int sound{-1};
    int consumed{};
    auto marker = path.rfind("\\genrl\\bank_");
    const char* target{};
    if (marker != std::string::npos) {
        target = path.c_str() + marker + 1;
    } else if (path.rfind("genrl\\bank_", 0) == 0) {
        target = path.c_str();
    } else {
        const auto relativeOffset = std::min<std::size_t>(
            file->pos_filedir,
            path.size()
        );
        const auto* relative = path.c_str() + relativeOffset;
        if (std::strncmp(relative, "bank_", 5) != 0) {
            return {};
        }
        target = relative;
    }
    const char* format = std::strncmp(target, "genrl\\", 6) == 0
        ? "genrl\\bank_%d\\sound_%d.wav%n"
        : "bank_%d\\sound_%d.wav%n";
    if (std::sscanf(
            target,
            format,
            &bank,
            &sound,
            &consumed
        ) != 2 || target[consumed] != '\0' ||
        (bank != kWeaponLocalBank && bank != kBulletHitLocalBank) ||
        sound < 1 ||
        sound > static_cast<int>(kMaxSounds)) {
        return {};
    }
    const auto runtimeBank =
        bank == kWeaponLocalBank
            ? 0
            : (bank == kBulletHitLocalBank ? 1 : -1);
    if (runtimeBank < 0) {
        return {};
    }
    return {runtimeBank, sound - 1};
}

enum class SourceFile {
    None,
    Archive,
    Lookup
};

SourceFile GetSourceFile(const modloader_file_t* file) {
    const auto path = NormalisePath(file);
    if (path.empty()) {
        return SourceFile::None;
    }
    const auto slash = path.find_last_of('\\');
    const auto name = path.substr(
        slash == std::string::npos ? 0 : slash + 1
    );
    if (name == "genrl") {
        return SourceFile::Archive;
    }
    if (name == "banklkup.dat") {
        return SourceFile::Lookup;
    }
    return SourceFile::None;
}

std::string GetFullPath(const modloader_file_t* file) {
    if (!file || !file->buffer || !gLoader || !gLoader->gamepath) {
        return {};
    }
    return std::string(gLoader->gamepath) + file->buffer;
}

void Deliver(const SoundReference& reference) {
    const auto callback = FindBackend();
    if (!callback ||
        reference.bank < 0 ||
        reference.bank >= static_cast<int>(kRuntimeBankCount) ||
        reference.sound < 0 ||
        reference.sound >= static_cast<int>(kMaxSounds)) {
        return;
    }
    const auto bankIndex = static_cast<std::size_t>(reference.bank);
    const auto soundIndex = static_cast<std::size_t>(reference.sound);
    callback(
        reference.bank,
        reference.sound,
        gPaths[bankIndex][soundIndex].c_str(),
        gInstalled[bankIndex][soundIndex] ? 1 : 0
    );
}

void DeliverSources() {
    const auto callback = FindSourcesBackend();
    if (!callback) {
        return;
    }
    callback(
        gArchiveInstalled ? gArchivePath.c_str() : "",
        gLookupInstalled ? gLookupPath.c_str() : ""
    );
}

int StoreFile(const modloader_file_t* file, bool installed) {
    const auto source = GetSourceFile(file);
    if (source != SourceFile::None) {
        auto& path = source == SourceFile::Archive
            ? gArchivePath
            : gLookupPath;
        auto& active = source == SourceFile::Archive
            ? gArchiveInstalled
            : gLookupInstalled;
        active = installed;
        path = installed ? GetFullPath(file) : std::string{};
        return 0;
    }
    const auto reference = GetSoundReference(file);
    if (reference.bank < 0 || reference.sound < 0) {
        return 0;
    }
    const auto bankIndex = static_cast<std::size_t>(reference.bank);
    const auto soundIndex = static_cast<std::size_t>(reference.sound);
    gInstalled[bankIndex][soundIndex] = installed;
    if (installed && gLoader && gLoader->gamepath) {
        gPaths[bankIndex][soundIndex] = GetFullPath(file);
    } else {
        gPaths[bankIndex][soundIndex].clear();
    }
    Deliver(reference);
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
    const auto reference = GetSoundReference(file);
    return (reference.bank >= 0 && reference.sound >= 0) ||
           GetSourceFile(file) != SourceFile::None
        ? 2
        : 0;
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
    if (!FindBackendModule()) {
        return;
    }
    DeliverSources();
    for (std::size_t bank = 0; bank < kRuntimeBankCount; ++bank) {
        for (std::size_t sound = 0; sound < kMaxSounds; ++sound) {
            if (gInstalled[bank][sound]) {
                Deliver({
                    static_cast<int>(bank),
                    static_cast<int>(sound)
                });
            }
        }
    }
}

const char* gExtensions[] = {"wav", "dat", ""};

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
    plugin->extable_len = 3;
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
    DeliverSources();
    for (std::size_t bank = 0; bank < kRuntimeBankCount; ++bank) {
        for (std::size_t sound = 0; sound < kMaxSounds; ++sound) {
            if (gInstalled[bank][sound]) {
                Deliver({
                    static_cast<int>(bank),
                    static_cast<int>(sound)
                });
            }
        }
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, void*) {
    return TRUE;
}
