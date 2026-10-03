#pragma once

#include <cstdio>

#include "SDL.h"

// Deterministic replay/dump harness for the level and world editors. Every entry
// point is a no-op unless the corresponding SMW_* environment variable is set;
// see port/EDITOR_REPLAY.md.
namespace editorharness {

void init();
void setDumper(void (*dumper)(FILE* out));
bool noLimit();

// SDL_GetKeyboardState(NULL) / SDL_GetMouseState(NULL, NULL); while active, the
// scripted state, since pushed events do not update SDL's.
const Uint8* keyboardState();
Uint32 mouseState();

// Ends one editor frame: dump, screenshot, advance, push the next frame's
// scripted events, then sleep `delay` ms unless SMW_NOLIMIT is set.
void frameDelay(unsigned delay);

} // namespace editorharness
