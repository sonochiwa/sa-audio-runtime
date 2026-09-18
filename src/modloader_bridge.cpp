#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
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
using DynamicSampleCallback = void(__cdecl*)(
    std::int32_t,
    std::int32_t,
    const char*,
    std::int32_t
);
using PackCallback = void(__cdecl*)(
    std::int32_t,
    const char*,
    std::int32_t
);
constexpr char kPluginVersion[] = "2.1.2";
// modloader.h: MODLOADER_FF_IS_DIRECTORY
constexpr std::uint32_t kFlagIsDirectory = 1;
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
HMODULE gDeliveredBackend{};

struct PackInfo {
    const char* name;
    int firstBank;
};

constexpr PackInfo kPacks[] = {
    {"feet", 0},
    {"genrl", 7},
    {"pain_a", 144},
    {"script", 147},
    {"spc_ea", 365},
    {"spc_fa", 411},
    {"spc_ga", 429},
    {"spc_na", 638},
    {"spc_pa", 690}
};

struct DynamicSoundReference {
    int bank{-1};
    int sound{-1};
};

std::map<std::uint32_t, std::string> gDynamicPaths;
std::array<std::string, std::size(kPacks)> gPackPaths{};
std::array<bool, std::size(kPacks)> gPackInstalled{};

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

DynamicSampleCallback FindDynamicSampleBackend() {
    const auto module = FindBackendModule();
    return module
        ? reinterpret_cast<DynamicSampleCallback>(GetProcAddress(
              module,
              "AudioRuntimeModLoaderDynamicSample"
          ))
        : nullptr;
}

PackCallback FindPackBackend() {
    const auto module = FindBackendModule();
    return module
        ? reinterpret_cast<PackCallback>(GetProcAddress(
              module,
              "AudioRuntimeModLoaderPack"
          ))
        : nullptr;
}

bool IsDirectory(const modloader_file_t* file) {
    return file && (file->flags & kFlagIsDirectory) != 0;
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
    for (const auto& pack : kPacks) {
        if (std::strcmp(pack.name, "genrl") == 0) {
            continue;
        }
        const auto marker = std::string("\\") + pack.name + "\\";
        const auto prefix = std::string(pack.name) + "\\";
        if (path.find(marker) != std::string::npos ||
            path.rfind(prefix, 0) == 0) {
            return {};
        }
    }
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

DynamicSoundReference GetDynamicSoundReference(
    const modloader_file_t* file
) {
    const auto path = NormalisePath(file);
    if (path.empty()) {
        return {};
    }
    for (std::size_t packIndex = 0;
         packIndex < std::size(kPacks);
         ++packIndex) {
        const auto& pack = kPacks[packIndex];
        const std::string prefix = std::string(pack.name) + "\\bank_";
        auto marker = path.rfind("\\" + prefix);
        const char* target{};
        if (marker != std::string::npos) {
            target = path.c_str() + marker + 1;
        } else if (path.rfind(prefix, 0) == 0) {
            target = path.c_str();
        } else {
            continue;
        }
        int localBank{-1};
        int sound{-1};
        int consumed{};
        const auto format = std::string(pack.name) +
            "\\bank_%d\\sound_%d.wav%n";
        if (std::sscanf(
                target,
                format.c_str(),
                &localBank,
                &sound,
                &consumed
            ) == 2 &&
            target[consumed] == '\0' &&
            localBank >= 1 &&
            sound >= 1 &&
            sound <= static_cast<int>(kMaxSounds)) {
            const auto globalBank = pack.firstBank + localBank - 1;
            const auto nextFirstBank = packIndex + 1 < std::size(kPacks)
                ? kPacks[packIndex + 1].firstBank
                : 700;
            if (globalBank < nextFirstBank) {
                return {globalBank, sound - 1};
            }
            return {};
        }
    }
    return {};
}

int GetPackSource(const modloader_file_t* file) {
    // A pack is an extension-less archive *file*. Matching a directory of the
    // same name would claim the whole "<mod>\GENRL\" tree, see GetSourceFile.
    if (IsDirectory(file)) {
        return -1;
    }
    const auto path = NormalisePath(file);
    if (path.empty()) {
        return -1;
    }
    const auto slash = path.find_last_of('\\');
    const auto name = path.substr(
        slash == std::string::npos ? 0 : slash + 1
    );
    for (int index = 0; index < static_cast<int>(std::size(kPacks));
         ++index) {
        if (name == kPacks[index].name) {
            return index;
        }
    }
    return -1;
}

enum class SourceFile {
    None,
    Archive,
    Lookup
};

SourceFile GetSourceFile(const modloader_file_t* file) {
    // Only a real file can be a replacement archive. The "" entry in
    // gExtensions makes modloader offer extension-less *directories* here as
    // well, and a mod laid out as "<mod>\GENRL\bank_137\sound_027.wav" has a
    // directory named exactly "genrl". Claiming it (even as CALLME) makes
    // modloader clear file.recursive, so the bank_*\sound_*.wav files below it
    // are never scanned -- neither by this bridge nor by gta3.std.bank.
    if (IsDirectory(file)) {
        return SourceFile::None;
    }
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

// Mirrors modloader's own modloader::IsAbsolutePath.
bool IsAbsolutePath(const char* path) {
    const auto first = path[0];
    if (first == '\\' || first == '/') {
        return true;
    }
    if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')) {
        return path[1] == ':' && (path[2] == '\\' || path[2] == '/');
    }
    return false;
}

std::string GetFullPath(const modloader_file_t* file) {
    if (!file || !file->buffer) {
        return {};
    }
    // file->buffer is normally relative to the game directory, but since
    // modloader 0.3.10 the mod folder may live next to the ASI instead of in
    // the game root (see MakePathRelativeTo in its loader.cpp). When that
    // folder is not under the game directory the loader hands out an absolute
    // path, and prefixing gamepath would corrupt it.
    if (IsAbsolutePath(file->buffer)) {
        return std::string(file->buffer);
    }
    if (!gLoader || !gLoader->gamepath) {
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

void DeliverDynamic(
    const DynamicSoundReference& reference,
    const std::string& path,
    bool installed
) {
    const auto callback = FindDynamicSampleBackend();
    if (callback && reference.bank >= 0 && reference.sound >= 0) {
        callback(
            reference.bank,
            reference.sound,
            path.c_str(),
            installed ? 1 : 0
        );
    }
}

void DeliverPack(int packId) {
    const auto callback = FindPackBackend();
    if (!callback || packId < 0 ||
        packId >= static_cast<int>(gPackPaths.size())) {
        return;
    }
    callback(
        packId,
        gPackPaths[static_cast<std::size_t>(packId)].c_str(),
        gPackInstalled[static_cast<std::size_t>(packId)] ? 1 : 0
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
        DeliverSources();
        return 0;
    }
    const auto packSource = GetPackSource(file);
    if (packSource >= 0) {
        const auto index = static_cast<std::size_t>(packSource);
        gPackInstalled[index] = installed;
        gPackPaths[index] = installed ? GetFullPath(file) : std::string{};
        DeliverPack(packSource);
        return 0;
    }
    const auto reference = GetSoundReference(file);
    if (reference.bank >= 0 && reference.sound >= 0) {
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
    const auto dynamic = GetDynamicSoundReference(file);
    if (dynamic.bank < 0 || dynamic.sound < 0) {
        return 0;
    }
    const auto key =
        static_cast<std::uint32_t>(dynamic.bank) << 16 |
        static_cast<std::uint16_t>(dynamic.sound);
    if (installed) {
        gDynamicPaths[key] = GetFullPath(file);
        DeliverDynamic(dynamic, gDynamicPaths[key], true);
    } else {
        gDynamicPaths.erase(key);
        DeliverDynamic(dynamic, {}, false);
    }
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
    // Every path this bridge understands is a file; never claim a directory.
    if (IsDirectory(file)) {
        return 0;
    }
    const auto reference = GetSoundReference(file);
    const auto dynamic = GetDynamicSoundReference(file);
    return (reference.bank >= 0 && reference.sound >= 0) ||
           (dynamic.bank >= 0 && dynamic.sound >= 0) ||
           GetSourceFile(file) != SourceFile::None ||
           GetPackSource(file) >= 0
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
    const auto backend = FindBackendModule();
    if (!backend || backend == gDeliveredBackend) {
        return;
    }
    gDeliveredBackend = backend;
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
    for (const auto& [key, path] : gDynamicPaths) {
        DeliverDynamic(
            {
                static_cast<int>(key >> 16),
                static_cast<int>(key & 0xFFFFu)
            },
            path,
            true
        );
    }
    for (int pack = 0; pack < static_cast<int>(gPackPaths.size()); ++pack) {
        if (gPackInstalled[static_cast<std::size_t>(pack)]) {
            DeliverPack(pack);
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
    gDeliveredBackend = FindBackendModule();
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
    for (const auto& [key, path] : gDynamicPaths) {
        DeliverDynamic(
            {
                static_cast<int>(key >> 16),
                static_cast<int>(key & 0xFFFFu)
            },
            path,
            true
        );
    }
    for (int pack = 0; pack < static_cast<int>(gPackPaths.size()); ++pack) {
        if (gPackInstalled[static_cast<std::size_t>(pack)]) {
            DeliverPack(pack);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, void*) {
    return TRUE;
}
