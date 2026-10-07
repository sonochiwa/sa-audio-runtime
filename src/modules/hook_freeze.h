#pragma once

#include <windows.h>

namespace runtime {

// MinHook suspends every other thread of the process while it enables or
// disables hooks. Two plugins doing that at once from their own threads
// suspend each other and the game hangs before its window appears; Audio
// Runtime and Borderless Mode did so in about one start in thirty right after
// a plugin was copied. Every plugin that holds this process-wide mutex
// around its MinHook calls waits for the others instead.
class HookFreezeLock {
public:
    HookFreezeLock();
    ~HookFreezeLock();
    HookFreezeLock(const HookFreezeLock&) = delete;
    HookFreezeLock& operator=(const HookFreezeLock&) = delete;

private:
    HANDLE mutex;
    bool owned;
};

} // namespace runtime
