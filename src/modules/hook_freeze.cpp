#include "modules/hook_freeze.h"

#include <cwchar>
#include <iterator>

namespace runtime {
namespace {

// The holder may itself be suspended for a moment by a plugin that does not
// take the mutex; after this long the freeze goes ahead without it.
constexpr DWORD kWaitMs = 10000;

} // namespace

HookFreezeLock::HookFreezeLock() : mutex(nullptr), owned(false) {
    wchar_t name[64]{};
    std::swprintf(name, std::size(name), L"Local\\GtaSaMinHookFreeze-%lu",
                  GetCurrentProcessId());
    mutex = CreateMutexW(nullptr, FALSE, name);
    if (!mutex) {
        return;
    }
    const DWORD result = WaitForSingleObject(mutex, kWaitMs);
    owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
}

HookFreezeLock::~HookFreezeLock() {
    if (owned) {
        ReleaseMutex(mutex);
    }
    if (mutex) {
        CloseHandle(mutex);
    }
}

} // namespace runtime
