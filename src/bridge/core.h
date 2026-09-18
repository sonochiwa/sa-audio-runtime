#pragma once

#include "bridge/prelude.h"

namespace bridge {

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
// modloader.h: MODLOADER_FF_IS_DIRECTORY
constexpr std::uint32_t kFlagIsDirectory = 1;
constexpr int kWeaponLocalBank = 137;
constexpr int kBulletHitLocalBank = 21;
constexpr std::size_t kRuntimeBankCount = 2;
constexpr std::size_t kMaxSounds = 400;
extern modloader_t* gLoader;
extern std::array< std::array<std::string, kMaxSounds>, kRuntimeBankCount > gPaths;
extern std::array< std::array<bool, kMaxSounds>, kRuntimeBankCount > gInstalled;
extern std::string gArchivePath;
extern std::string gLookupPath;
extern bool gArchiveInstalled;
extern bool gLookupInstalled;
extern HMODULE gDeliveredBackend;

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
extern std::map<std::uint32_t, std::string> gDynamicPaths;
extern std::array<std::string, std::size(kPacks)> gPackPaths;
extern std::array<bool, std::size(kPacks)> gPackInstalled;

struct SoundReference {
    int bank{-1};
    int sound{-1};
};
HMODULE FindBackendModule();
SampleCallback FindBackend();
SourcesCallback FindSourcesBackend();
DynamicSampleCallback FindDynamicSampleBackend();
PackCallback FindPackBackend();
bool IsDirectory(const modloader_file_t* file);
std::string NormalisePath(const modloader_file_t* file);
SoundReference GetSoundReference(const modloader_file_t* file);
DynamicSoundReference GetDynamicSoundReference( const modloader_file_t* file );
int GetPackSource(const modloader_file_t* file);

enum class SourceFile {
    None,
    Archive,
    Lookup
};
SourceFile GetSourceFile(const modloader_file_t* file);
bool IsAbsolutePath(const char* path);
std::string GetFullPath(const modloader_file_t* file);
void Deliver(const SoundReference& reference);
void DeliverSources();
void DeliverDynamic( const DynamicSoundReference& reference, const std::string& path, bool installed );
void DeliverPack(int packId);
int StoreFile(const modloader_file_t* file, bool installed);
const char* __cdecl GetAuthor(modloader_plugin_t*);
const char* __cdecl GetVersion(modloader_plugin_t*);
int __cdecl OnStartup(modloader_plugin_t* plugin);
int __cdecl OnShutdown(modloader_plugin_t*);
int __cdecl GetBehaviour( modloader_plugin_t*, modloader_file_t* file );
int __cdecl InstallFile( modloader_plugin_t*, const modloader_file_t* file );
int __cdecl ReinstallFile( modloader_plugin_t*, const modloader_file_t* file );
int __cdecl UninstallFile( modloader_plugin_t*, const modloader_file_t* file );
void __cdecl Update(modloader_plugin_t*);

extern const char* gExtensions[];

} // namespace bridge
