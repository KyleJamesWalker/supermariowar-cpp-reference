#include "EditorHarness.h"

#include "RandomNumberGenerator.h"

#include "SDL.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern SDL_Surface* screen;

namespace editorharness {
namespace {

enum class EventKind { Key, MouseButton, MouseMove };

struct ScriptEvent {
    unsigned frame;
    EventKind kind;
    bool down;
    std::string keyName;
    Uint8 button;
    int x, y;
};

const unsigned QUIT_GRACE_FRAMES = 60;

bool g_active = false;
bool g_noLimit = false;
long g_maxFrames = -1;
FILE* g_dump = nullptr;
std::set<unsigned> g_shotFrames;
std::string g_shotDir = ".";
std::vector<ScriptEvent> g_events;
size_t g_nextEvent = 0;
unsigned g_frame = 0;
void (*g_dumper)(FILE*) = nullptr;

Uint32 g_buttons = 0;
SDL_Keymod g_mod = KMOD_NONE;
int g_mouseX = 0, g_mouseY = 0;
Uint8 g_keys[SDL_NUM_SCANCODES] = {};

const char* env(const char* name)
{
    const char* value = getenv(name);
    return (value && *value) ? value : nullptr;
}

[[noreturn]] void fail(const char* path, int lineno, const char* msg)
{
    fprintf(stderr, "[editorharness] %s:%d: %s\n", path, lineno, msg);
    exit(2);
}

Uint8 parseButton(const std::string& name, const char* path, int lineno)
{
    if (name == "left")
        return SDL_BUTTON_LEFT;
    if (name == "middle")
        return SDL_BUTTON_MIDDLE;
    if (name == "right")
        return SDL_BUTTON_RIGHT;
    fail(path, lineno, "expected left, middle or right");
}

void loadReplay(const char* path)
{
    std::ifstream in(path);
    if (!in) {
        fprintf(stderr, "[editorharness] cannot open replay %s\n", path);
        exit(2);
    }

    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        lineno++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;

        std::istringstream ls(line);
        ScriptEvent ev{};
        std::string verb;
        if (!(ls >> ev.frame >> verb))
            fail(path, lineno, "malformed line");

        if (verb == "down" || verb == "up") {
            ev.kind = EventKind::Key;
            ev.down = verb == "down";
            std::getline(ls, ev.keyName);
            ev.keyName.erase(0, ev.keyName.find_first_not_of(" \t"));
            if (ev.keyName.empty())
                fail(path, lineno, "expected '<frame> <down|up> <key>'");
        } else if (verb == "mousedown" || verb == "mouseup") {
            ev.kind = EventKind::MouseButton;
            ev.down = verb == "mousedown";
            std::string button;
            if (!(ls >> button >> ev.x >> ev.y))
                fail(path, lineno, "expected '<frame> <mousedown|mouseup> <button> <x> <y>'");
            ev.button = parseButton(button, path, lineno);
        } else if (verb == "mousemove") {
            ev.kind = EventKind::MouseMove;
            if (!(ls >> ev.x >> ev.y))
                fail(path, lineno, "expected '<frame> mousemove <x> <y>'");
        } else {
            fail(path, lineno, "unknown event");
        }

        if (!g_events.empty() && ev.frame < g_events.back().frame)
            fail(path, lineno, "frames must be non-decreasing");
        g_events.push_back(ev);
    }
}

SDL_Keymod modifierFor(SDL_Keycode key)
{
    switch (key) {
    case SDLK_LSHIFT: return KMOD_LSHIFT;
    case SDLK_RSHIFT: return KMOD_RSHIFT;
    case SDLK_LCTRL: return KMOD_LCTRL;
    case SDLK_RCTRL: return KMOD_RCTRL;
    case SDLK_LALT: return KMOD_LALT;
    case SDLK_RALT: return KMOD_RALT;
    default: return KMOD_NONE;
    }
}

void pushEvent(const ScriptEvent& ev)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));

    if (ev.kind == EventKind::Key) {
        SDL_Keycode key = SDL_GetKeyFromName(ev.keyName.c_str());
        if (key == SDLK_UNKNOWN) {
            fprintf(stderr, "[editorharness] unknown SDL key name '%s'\n", ev.keyName.c_str());
            exit(2);
        }

        SDL_Keymod mod = modifierFor(key);
        if (ev.down)
            g_mod = (SDL_Keymod)(g_mod | mod);
        else
            g_mod = (SDL_Keymod)(g_mod & ~mod);

        event.type = ev.down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.state = ev.down ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.sym = key;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
        event.key.keysym.mod = g_mod;
        g_keys[event.key.keysym.scancode] = ev.down ? 1 : 0;
    } else if (ev.kind == EventKind::MouseButton) {
        if (ev.down)
            g_buttons |= SDL_BUTTON(ev.button);
        else
            g_buttons &= ~SDL_BUTTON(ev.button);

        event.type = ev.down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.button = ev.button;
        event.button.state = ev.down ? SDL_PRESSED : SDL_RELEASED;
        event.button.clicks = 1;
        event.button.x = ev.x;
        event.button.y = ev.y;
        g_mouseX = ev.x;
        g_mouseY = ev.y;
    } else {
        event.type = SDL_MOUSEMOTION;
        event.motion.state = g_buttons;
        event.motion.x = ev.x;
        event.motion.y = ev.y;
        event.motion.xrel = ev.x - g_mouseX;
        event.motion.yrel = ev.y - g_mouseY;
        g_mouseX = ev.x;
        g_mouseY = ev.y;
    }

    SDL_PushEvent(&event);
}

void pushFrameEvents()
{
    while (g_nextEvent < g_events.size() && g_events[g_nextEvent].frame <= g_frame) {
        if (g_events[g_nextEvent].frame == g_frame)
            pushEvent(g_events[g_nextEvent]);
        g_nextEvent++;
    }
}

void pushQuit()
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
}

} // namespace


void init()
{
    if (const char* seed = env("SMW_SEED")) {
        unsigned value = (unsigned)strtoul(seed, nullptr, 0);
        RandomNumberGenerator::generator().reseed(value);
        srand(value);
        RandomNumberGenerator::resetCallCount();
    }

    g_noLimit = env("SMW_NOLIMIT") != nullptr;

    if (const char* frames = env("SMW_FRAMES"))
        g_maxFrames = strtol(frames, nullptr, 10);

    if (const char* replay = env("SMW_REPLAY"))
        loadReplay(replay);

    if (const char* dump = env("SMW_DUMP")) {
        g_dump = fopen(dump, "a");
        if (!g_dump) {
            fprintf(stderr, "[editorharness] cannot open dump %s\n", dump);
            exit(2);
        }
    }

    if (const char* shots = env("SMW_SHOT_FRAMES")) {
        std::stringstream ss(shots);
        std::string item;
        while (std::getline(ss, item, ','))
            if (!item.empty())
                g_shotFrames.insert((unsigned)strtoul(item.c_str(), nullptr, 10));
    }
    if (const char* dir = env("SMW_SHOT_DIR"))
        g_shotDir = dir;

    g_active = true;
    pushFrameEvents();
}

void setDumper(void (*dumper)(FILE* out))
{
    g_dumper = dumper;
}

bool noLimit()
{
    return g_noLimit;
}

void frameDelay(unsigned delay)
{
    if (g_active) {
        if (g_maxFrames < 0 || (long)g_frame < g_maxFrames) {
            if (g_dump) {
                fprintf(g_dump, "F %u\n", g_frame);
                if (g_dumper)
                    g_dumper(g_dump);
                fprintf(g_dump, "R calls=%llu last=%u\n",
                    (unsigned long long)RandomNumberGenerator::callCount(), RandomNumberGenerator::lastValue());
                fflush(g_dump);
            }

            if (g_shotFrames.count(g_frame)) {
                std::string path = g_shotDir + "/frame_" + std::to_string(g_frame) + ".bmp";
                if (SDL_SaveBMP(screen, path.c_str()) != 0)
                    fprintf(stderr, "[editorharness] cannot save %s: %s\n", path.c_str(), SDL_GetError());
            }
        }

        g_frame++;

        if (g_maxFrames >= 0 && (long)g_frame >= g_maxFrames) {
            if (g_dump) {
                fclose(g_dump);
                g_dump = nullptr;
            }
            if ((long)g_frame >= g_maxFrames + (long)QUIT_GRACE_FRAMES) {
                fprintf(stderr, "[editorharness] editor ignored SDL_QUIT for %u frames\n", QUIT_GRACE_FRAMES);
                exit(3);
            }
            pushQuit();
        } else {
            pushFrameEvents();
        }
    }

    if (!g_noLimit)
        SDL_Delay(delay);
}

const Uint8* keyboardState()
{
    return g_active ? g_keys : SDL_GetKeyboardState(NULL);
}

Uint32 mouseState()
{
    return g_active ? g_buttons : SDL_GetMouseState(NULL, NULL);
}

} // namespace editorharness
