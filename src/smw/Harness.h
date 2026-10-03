#pragma once

// Deterministic replay/dump harness. Every entry point is a no-op unless the
// corresponding SMW_* environment variable is set; see port/REPLAY.md.
union SDL_Event;

namespace harness {

void init();
unsigned libcSeed(unsigned fallback);

bool noLimit();
const char* forcedMap();

void frameStart();
// Blocking waits take the next replay event immediately instead of waiting for input.
void waitEvent(SDL_Event* event);
void frameEnd();

} // namespace harness
