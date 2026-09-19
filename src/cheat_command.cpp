#include "cheat_command.h"

#include <windows.h>

#include <cstring>

namespace {

constexpr size_t kWordCapacity = 32;

// The same idea as CCheat::DoCheats: the last keys typed are kept in order
// and the word matches as soon as it is the tail of that history. The
// history is filled by a WH_KEYBOARD hook on the game window's thread, so
// it sees every key in the order the game does, whether or not the SA-MP
// chat box is open.
SRWLOCK gLock = SRWLOCK_INIT;
char gWord[kWordCapacity]{};
size_t gWordLength{};
char gHistory[kWordCapacity]{};
size_t gHistoryLength{};
volatile LONG gPending{};
HHOOK gHook{};

char KeyToChar(WPARAM virtualKey) {
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        return static_cast<char>(virtualKey);
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        return static_cast<char>(virtualKey);
    }
    if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) {
        return static_cast<char>('0' + (virtualKey - VK_NUMPAD0));
    }
    return 0;
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    // Bit 31 is the transition state (set on release), bit 30 the previous
    // state (set on auto-repeat). Only fresh presses count.
    if (code == HC_ACTION && !(lParam & 0xC0000000)) {
        const char typed = KeyToChar(wParam);
        if (typed) {
            AcquireSRWLockExclusive(&gLock);
            if (gHistoryLength == kWordCapacity - 1) {
                std::memmove(gHistory, gHistory + 1, kWordCapacity - 2);
                --gHistoryLength;
            }
            gHistory[gHistoryLength++] = typed;
            if (gWordLength != 0 && gHistoryLength >= gWordLength &&
                std::memcmp(
                    gHistory + gHistoryLength - gWordLength,
                    gWord,
                    gWordLength
                ) == 0) {
                gHistoryLength = 0;
                InterlockedExchange(&gPending, 1);
            }
            ReleaseSRWLockExclusive(&gLock);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

BOOL CALLBACK FindGameWindow(HWND window, LPARAM result) {
    DWORD processId{};
    const DWORD threadId = GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId() || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    *reinterpret_cast<DWORD*>(result) = threadId;
    return FALSE;
}

} // namespace

bool CheatCommandInstall() {
    if (gHook) {
        return true;
    }

    DWORD threadId{};
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&threadId));
    if (threadId == 0) {
        return false;
    }

    // A thread hook inside the calling process takes no module handle.
    gHook = SetWindowsHookExW(WH_KEYBOARD, KeyboardProc, nullptr, threadId);
    return gHook != nullptr;
}

void CheatCommandSetWord(const char* word) {
    char normalized[kWordCapacity]{};
    size_t length{};
    for (const char* c = word ? word : ""; *c && length < kWordCapacity - 1; ++c) {
        if (*c >= 'a' && *c <= 'z') {
            normalized[length++] = static_cast<char>(*c - 'a' + 'A');
        } else if ((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')) {
            normalized[length++] = *c;
        }
    }

    AcquireSRWLockExclusive(&gLock);
    std::memcpy(gWord, normalized, kWordCapacity);
    gWordLength = length;
    gHistoryLength = 0;
    ReleaseSRWLockExclusive(&gLock);
}

bool CheatCommandConsume() {
    return InterlockedExchange(&gPending, 0) != 0;
}
