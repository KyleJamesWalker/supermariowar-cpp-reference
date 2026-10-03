#pragma once

#ifdef SDL2_USE_MIXERX
#include "SDL_mixer_ext.h"
#else
#include "SDL_mixer.h"
#endif

#include <array>
#include <bitset>
#include <filesystem>
#include <string>
#include <vector>

struct MixDeleter {
    void operator()(Mix_Chunk* ptr) const noexcept;
    void operator()(Mix_Music* ptr) const noexcept;
};
using MixChunkPtr = std::unique_ptr<Mix_Chunk, MixDeleter>;
using MixMusicPtr = std::unique_ptr<Mix_Music, MixDeleter>;


bool sfx_init();
// Clock used for the sfxSound::play() retrigger throttle; the replay harness swaps it.
extern Uint32 (SDLCALL *sfx_ticks)(void);
extern bool sfx_ignore_channel_failure;
// Seeded replays replace SDL_mixer playback state with a virtual mixer driven by sfx_ticks,
// and log every sound command to sfx_events (port/REPLAY.md, "Sound").
extern bool sfx_virtual_mixer;
extern std::vector<std::string> sfx_events;
void sfx_virtual_advance();
void sfx_close();
void sfx_stopallsounds();
void sfx_setmusicvolume(int volume);
void sfx_setsoundvolume(int volume);
bool sfx_canPlayAudio();


class sfxSound {
public:
    static constexpr int k_channels = 16;

    sfxSound() = default;
    sfxSound(const std::filesystem::path& path);

    bool play();
    void playLoop(int iLoop);
    void stop();

    bool isPlaying() const { return m_channels.any(); }

    static void onChannelFinished(int channel);

private:
    MixChunkPtr m_sfx;
    std::string m_name;
    std::bitset<k_channels> m_channels;
    size_t m_last_start_time = 0;

    static inline std::array<sfxSound*, k_channels> s_channels {};
};


class sfxMusic {
public:
    sfxMusic() = default;
    sfxMusic(const std::filesystem::path& path);

    void play(bool fPlayonce, bool fResume);
    void stop();

    void togglePause();

    bool isPlaying() const;

private:
    MixMusicPtr m_music;
    std::string m_name;
    bool m_paused = false;
};
