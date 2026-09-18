#include "bridge/bridge.h"

namespace bridge {

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

} // namespace bridge
