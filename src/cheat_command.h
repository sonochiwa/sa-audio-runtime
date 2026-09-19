#pragma once

// Hooks the keyboard of the thread that owns the game window. Returns false
// while that window does not exist yet; call again later.
bool CheatCommandInstall();

// The word to watch for, typed in game like a single-player cheat. Letters
// and digits only, case-insensitive; an empty word disables the command.
void CheatCommandSetWord(const char* word);

// True once for every complete word typed since the previous call.
bool CheatCommandConsume();
