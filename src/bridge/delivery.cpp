#include "bridge/bridge.h"
#include "version.h"

namespace bridge {

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
    return PLUGIN_VERSION;
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

} // namespace bridge
