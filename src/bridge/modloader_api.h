#pragma once

#include <cstddef>
#include <cstdint>

// The subset of modloader.h the bridge uses, declared here so no ModLoader
// SDK is needed to build.
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
