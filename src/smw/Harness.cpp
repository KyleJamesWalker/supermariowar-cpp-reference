#include "Harness.h"

#include "eyecandy.h"
#include "GameMode.h"
#include "GameValues.h"
#include "GSGameplay.h"
#include "GSMenu.h"
#include "GSSplashScreen.h"
#include "MapList.h"
#include "ObjectContainer.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "Score.h"
#include "sfx.h"

#include "SDL.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern CGameValues game_values;
extern std::vector<CPlayer*> players;
extern CObjectContainer noncolcontainer;
extern CObjectContainer objectcontainer[3];
extern CEyecandyContainer eyecandy[3];
extern SDL_Surface* screen;


struct HarnessAccess {
    static void dumpPlayer(FILE* out, const CPlayer& p) {
        fprintf(out, "P id=%d team=%d ix=%d iy=%d fx=%.4f fy=%.4f velx=%.4f vely=%.4f state=%d score=%d powerup=%d inair=%d\n",
            p.globalID, p.teamID, p.ix, p.iy,
            (double)p.fx, (double)p.fy, (double)p.velx, (double)p.vely,
            (int)p.state, p.score ? p.score->score : 0, p.powerup, p.inair ? 1 : 0);
    }
};

namespace harness {
namespace {

enum class EventKind { Key, JoyAxis, JoyButton, JoyHat };

struct ReplayEvent {
    unsigned frame;
    EventKind kind;
    bool down;
    std::string keyName;
    int device, index, value;
};

constexpr int VIRTUAL_AXES = 6;
constexpr int VIRTUAL_BUTTONS = 16;
constexpr int VIRTUAL_HATS = 1;

bool g_seeded = false;
unsigned g_seed = 0;
bool g_noLimit = false;
std::string g_map;
long g_maxFrames = -1;
FILE* g_dump = nullptr;
std::set<unsigned> g_shotFrames;
std::string g_shotDir = ".";
unsigned g_shotEvery = 0;
unsigned g_shotFrom = 0;
unsigned g_shotTo = UINT_MAX;
FILE* g_shotStream = nullptr;
std::vector<ReplayEvent> g_events;
int g_joysticks = 0;
bool g_replay = false;
size_t g_nextEvent = 0;
unsigned g_frame = 0;

const char* env(const char* name)
{
    const char* value = getenv(name);
    return (value && *value) ? value : nullptr;
}

Uint32 SDLCALL virtualTicks()
{
    return 1000 + g_frame * WAITTIME;
}

void loadReplay(const char* path)
{
    std::ifstream in(path);
    if (!in) {
        fprintf(stderr, "[harness] cannot open replay %s\n", path);
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
        ReplayEvent ev {};
        std::string dir;
        if (!(ls >> ev.frame >> dir)) {
            fprintf(stderr, "[harness] %s:%d: malformed line\n", path, lineno);
            exit(2);
        }
        if (dir == "jaxis" || dir == "jbutton" || dir == "jhat") {
            ev.kind = dir == "jaxis" ? EventKind::JoyAxis : dir == "jbutton" ? EventKind::JoyButton : EventKind::JoyHat;
            std::string trailing;
            int indexLimit = dir == "jaxis" ? VIRTUAL_AXES : dir == "jbutton" ? VIRTUAL_BUTTONS : VIRTUAL_HATS;
            if (!(ls >> ev.device >> ev.index >> ev.value) || (ls >> trailing) || ev.device < 0 || ev.device > 7
                    || ev.index < 0 || ev.index >= indexLimit
                    || (dir == "jaxis" && (ev.value < -32768 || ev.value > 32767))
                    || (dir == "jbutton" && ev.value != 0 && ev.value != 1)
                    || (dir == "jhat" && (ev.value < 0 || ev.value > 15))) {
                fprintf(stderr, "[harness] %s:%d: expected '<frame> %s <dev> <index> <value>'\n", path, lineno, dir.c_str());
                exit(2);
            }
            g_joysticks = std::max(g_joysticks, ev.device + 1);
        } else {
            ev.kind = EventKind::Key;
            std::getline(ls, ev.keyName);
            ev.keyName.erase(0, ev.keyName.find_first_not_of(" \t"));
            if ((dir != "down" && dir != "up") || ev.keyName.empty()) {
                fprintf(stderr, "[harness] %s:%d: expected '<frame> <down|up> <key>'\n", path, lineno);
                exit(2);
            }
            ev.down = dir == "down";
        }
        if (!g_events.empty() && ev.frame < g_events.back().frame) {
            fprintf(stderr, "[harness] %s:%d: frames must be non-decreasing\n", path, lineno);
            exit(2);
        }
        g_events.push_back(ev);
    }
}

void attachJoysticks()
{
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    for (int i = 0; i < g_joysticks; i++) {
        if (SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, VIRTUAL_AXES, VIRTUAL_BUTTONS, VIRTUAL_HATS) != i) {
            fprintf(stderr, "[harness] cannot attach virtual joystick %d: %s\n", i, SDL_GetError());
            exit(2);
        }
    }
}

void fillJoystick(const ReplayEvent& ev, SDL_Event& event)
{
    memset(&event, 0, sizeof(event));
    if (ev.kind == EventKind::JoyAxis) {
        event.type = SDL_JOYAXISMOTION;
        event.jaxis.which = ev.device;
        event.jaxis.axis = (Uint8)ev.index;
        event.jaxis.value = (Sint16)ev.value;
    } else if (ev.kind == EventKind::JoyButton) {
        event.type = ev.value ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
        event.jbutton.which = ev.device;
        event.jbutton.button = (Uint8)ev.index;
        event.jbutton.state = ev.value ? SDL_PRESSED : SDL_RELEASED;
    } else {
        event.type = SDL_JOYHATMOTION;
        event.jhat.which = ev.device;
        event.jhat.hat = (Uint8)ev.index;
        event.jhat.value = (Uint8)ev.value;
    }
}

void fillKey(const ReplayEvent& ev, SDL_Event& event)
{
    SDL_Keycode key = SDL_GetKeyFromName(ev.keyName.c_str());
    if (key == SDLK_UNKNOWN) {
        fprintf(stderr, "[harness] unknown SDL key name '%s'\n", ev.keyName.c_str());
        exit(2);
    }

    memset(&event, 0, sizeof(event));
    event.type = ev.down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = ev.down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    event.key.keysym.mod = KMOD_NONE;
}

void fillEvent(const ReplayEvent& ev, SDL_Event& event)
{
    if (ev.kind == EventKind::Key)
        fillKey(ev, event);
    else
        fillJoystick(ev, event);
}

const char* stateName()
{
    GameState* current = GameStateManager::instance().currentState;
    if (current == &SplashScreenState::instance())
        return "splash";
    if (current == &MenuState::instance())
        return "menu";
    if (current == &GameplayState::instance())
        return "gameplay";
    return "other";
}

void dumpFrame()
{
    fprintf(g_dump, "F %u %s\n", g_frame, stateName());

    GameState* current = GameStateManager::instance().currentState;
    if (current == &MenuState::instance()) {
        const MenuState& menu = MenuState::instance();
        fprintf(g_dump, "M %s focus=%d modifying=%d\n",
            menu.harnessMenuName(), menu.harnessFocusIndex(), menu.harnessModifying() ? 1 : 0);
    } else if (current == &GameplayState::instance()) {
        CGameMode* mode = game_values.gamemode;
        fprintf(g_dump, "G mode=%d gameover=%d winner=%d\n",
            (int)mode->gamemode, mode->gameover ? 1 : 0, mode->winningteam);
        for (const CPlayer* player : players)
            HarnessAccess::dumpPlayer(g_dump, *player);
        fprintf(g_dump, "O noncol=%zu obj0=%zu obj1=%zu obj2=%zu ec0=%zu ec1=%zu ec2=%zu\n",
            noncolcontainer.list().size(),
            objectcontainer[0].list().size(), objectcontainer[1].list().size(), objectcontainer[2].list().size(),
            eyecandy[0].eyecandies.size(), eyecandy[1].eyecandies.size(), eyecandy[2].eyecandies.size());
    }

    for (const std::string& line : sfx_events)
        fprintf(g_dump, "%s\n", line.c_str());

    fprintf(g_dump, "R calls=%llu last=%u\n",
        (unsigned long long)RandomNumberGenerator::callCount(), RandomNumberGenerator::lastValue());
}

// Raw ARGB8888 rows (BGRA bytes on little-endian), the format tools/replay_video.py reads.
void writeScreenRaw()
{
    SDL_Surface* surface = screen->format->format == SDL_PIXELFORMAT_ARGB8888
        ? screen : SDL_ConvertSurfaceFormat(screen, SDL_PIXELFORMAT_ARGB8888, 0);
    bool ok = surface != nullptr;
    if (ok) {
        SDL_LockSurface(surface);
        for (int y = 0; ok && y < surface->h; y++)
            ok = fwrite((const char*)surface->pixels + y * surface->pitch, 4, surface->w, g_shotStream) == (size_t)surface->w;
        SDL_UnlockSurface(surface);
        if (surface != screen)
            SDL_FreeSurface(surface);
    }
    if (!ok || fflush(g_shotStream) != 0) {
        fprintf(stderr, "[harness] cannot write shot stream at frame %u\n", g_frame);
        exit(2);
    }
}

} // namespace


void init()
{
    if (const char* seed = env("SMW_SEED")) {
        g_seeded = true;
        g_seed = (unsigned)strtoul(seed, nullptr, 0);
        RandomNumberGenerator::generator().reseed(g_seed);
        srand(g_seed);
        RandomNumberGenerator::resetCallCount();
        sfx_ticks = &virtualTicks;
        sfx_ignore_channel_failure = true;
        sfx_virtual_mixer = true;
    }

    g_noLimit = env("SMW_NOLIMIT") != nullptr;

    if (const char* map = env("SMW_MAP"))
        g_map = map;

    if (const char* frames = env("SMW_FRAMES"))
        g_maxFrames = strtol(frames, nullptr, 10);

    if (const char* replay = env("SMW_REPLAY")) {
        loadReplay(replay);
        g_replay = true;
    }

    if (g_joysticks > 0)
        attachJoysticks();

    if (const char* dump = env("SMW_DUMP")) {
        g_dump = fopen(dump, "a");
        if (!g_dump) {
            fprintf(stderr, "[harness] cannot open dump %s\n", dump);
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
    if (const char* range = env("SMW_SHOT_RANGE")) {
        char* end = nullptr;
        g_shotFrom = (unsigned)strtoul(range, &end, 10);
        if (*end == '-' && end[1])
            g_shotTo = (unsigned)strtoul(end + 1, nullptr, 10);
        g_shotEvery = 1;
    }
    if (const char* every = env("SMW_SHOT_EVERY"))
        g_shotEvery = std::max(1ul, strtoul(every, nullptr, 10));
    if (const char* stream = env("SMW_SHOT_STREAM")) {
        g_shotStream = fopen(stream, "ab");
        if (!g_shotStream) {
            fprintf(stderr, "[harness] cannot open shot stream %s\n", stream);
            exit(2);
        }
    }
}

unsigned libcSeed(unsigned fallback)
{
    return g_seeded ? g_seed : fallback;
}

bool noLimit()
{
    return g_noLimit;
}

const char* forcedMap()
{
    return g_map.empty() ? nullptr : g_map.c_str();
}

void frameStart()
{
    sfx_virtual_advance();

    while (g_nextEvent < g_events.size() && g_events[g_nextEvent].frame <= g_frame) {
        if (g_events[g_nextEvent].frame == g_frame) {
            SDL_Event event;
            fillEvent(g_events[g_nextEvent], event);
            SDL_PushEvent(&event);
        }
        g_nextEvent++;
    }
}

void waitEvent(SDL_Event* event)
{
    if (!g_replay) {
        SDL_WaitEvent(event);
        return;
    }
    if (g_nextEvent >= g_events.size()) {
        fprintf(stderr, "[harness] blocking wait at frame %u but the replay has no events left\n", g_frame);
        exit(2);
    }
    fillEvent(g_events[g_nextEvent++], *event);
}

void frameEnd()
{
    if (g_dump) {
        dumpFrame();
        fflush(g_dump);
    }
    sfx_events.clear();

    bool periodic = g_shotEvery > 0 && g_frame >= g_shotFrom && g_frame <= g_shotTo
        && (g_frame - g_shotFrom) % g_shotEvery == 0;
    if (periodic && g_shotStream)
        writeScreenRaw();
    if (g_shotFrames.count(g_frame) || (periodic && !g_shotStream)) {
        std::string path = g_shotDir + "/frame_" + std::to_string(g_frame) + ".bmp";
        if (SDL_SaveBMP(screen, path.c_str()) != 0)
            fprintf(stderr, "[harness] cannot save %s: %s\n", path.c_str(), SDL_GetError());
    }

    g_frame++;
    if (g_maxFrames >= 0 && (long)g_frame >= g_maxFrames) {
        game_values.appstate = AppState::Quit;
        if (g_dump) {
            fclose(g_dump);
            g_dump = nullptr;
        }
    }
}

} // namespace harness
