// The ModLoader companion. ModLoader hands the plugin every file under a
// mod's `audio` tree; the bridge records the WAV replacements and pack
// sources it sees and delivers them to AudioRuntime.asi through its exported
// callbacks, replaying everything it holds when the runtime asks.

#include "bridge/bridge.h"
#include "version.h"

using namespace bridge;

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
    plugin->name = "Audio Runtime ModLoader Bridge";
    plugin->author = "sonochiwa";
    plugin->version = PLUGIN_VERSION;
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
