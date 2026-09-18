#include "bridge/bridge.h"

namespace bridge {

const char* gExtensions[] = {"wav", "dat", ""};

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

std::map<std::uint32_t, std::string> gDynamicPaths;
std::array<std::string, std::size(kPacks)> gPackPaths{};
std::array<bool, std::size(kPacks)> gPackInstalled{};

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

} // namespace bridge
