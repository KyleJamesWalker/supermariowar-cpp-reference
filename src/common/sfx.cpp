#include "sfx.h"

#include "SDL.h"

#include <format>
#include <iostream>
#include <string>

namespace fs = std::filesystem;


bool fResumeMusic = true;
Uint32 (SDLCALL *sfx_ticks)(void) = &SDL_GetTicks;
bool sfx_ignore_channel_failure = false;
bool sfx_virtual_mixer = false;
std::vector<std::string> sfx_events;
extern void SDLCALL musicfinished();


namespace {
struct VirtualChannel {
    Mix_Chunk* chunk = nullptr;
    bool forever = false;
    Uint32 end = 0;
};
std::array<VirtualChannel, sfxSound::k_channels> v_channels;

struct VirtualMusic {
    Mix_Music* music = nullptr;
    bool forever = false;
    bool paused = false;
    Uint32 end = 0;
    Uint32 remaining = 0;
} v_music;

void logEvent(std::string line)
{
    if (sfx_virtual_mixer)
        sfx_events.push_back(std::move(line));
}

// The data-relative part of a path ("sfx/packs/Classic/jump.wav"), identical for any data root.
std::string dataRelative(const std::string& path)
{
    size_t pos = path.rfind("data/");
    while (pos != std::string::npos && pos > 0 && path[pos - 1] != '/')
        pos = pos == 0 ? std::string::npos : path.rfind("data/", pos - 1);
    return pos == std::string::npos ? path : path.substr(pos + 5);
}

Uint32 chunkDurationMs(const Mix_Chunk* chunk)
{
    int frequency = 0, channels = 0;
    Uint16 format = 0;
    Mix_QuerySpec(&frequency, &format, &channels);
    const Uint64 bytesPerSecond = (Uint64)frequency * channels * (SDL_AUDIO_BITSIZE(format) / 8);
    return bytesPerSecond ? (Uint32)((Uint64)chunk->alen * 1000 / bytesPerSecond) : 0;
}

int mixPlayChannel(Mix_Chunk* chunk, int loops)
{
    if (!sfx_virtual_mixer)
        return Mix_PlayChannel(-1, chunk, loops);

    if (!chunk)  // Mix_PlayChannel rejects NULL chunks
        return -1;

    for (int i = 0; i < sfxSound::k_channels; i++) {
        VirtualChannel& ch = v_channels[i];
        if (ch.chunk)
            continue;
        ch.chunk = chunk;
        ch.forever = loops < 0;
        ch.end = sfx_ticks() + chunkDurationMs(chunk) * (Uint32)(loops + 1);
        return i;
    }
    return -1;
}

void mixHaltChannel(int channel)
{
    if (!sfx_virtual_mixer) {
        Mix_HaltChannel(channel);
        return;
    }

    for (int i = 0; i < sfxSound::k_channels; i++) {
        if ((channel < 0 || channel == i) && v_channels[i].chunk) {
            v_channels[i].chunk = nullptr;
            sfxSound::onChannelFinished(i);
        }
    }
}
} // namespace


void sfx_virtual_advance()
{
    if (!sfx_virtual_mixer)
        return;

    const Uint32 now = sfx_ticks();
    for (int i = 0; i < sfxSound::k_channels; i++) {
        VirtualChannel& ch = v_channels[i];
        if (ch.chunk && !ch.forever && (Sint32)(now - ch.end) >= 0) {
            ch.chunk = nullptr;
            logEvent(std::format("S done ch={}", i));
            sfxSound::onChannelFinished(i);
        }
    }

    if (v_music.music && !v_music.forever && !v_music.paused && (Sint32)(now - v_music.end) >= 0) {
        v_music.music = nullptr;
        logEvent("S musicdone");
        musicfinished();
    }
}


void MixDeleter::operator()(Mix_Chunk* ptr) const noexcept
{
    for (VirtualChannel& ch : v_channels) {
        if (ch.chunk == ptr)
            ch.chunk = nullptr;
    }
    Mix_FreeChunk(ptr);
}

void MixDeleter::operator()(Mix_Music* ptr) const noexcept
{
    if (v_music.music == ptr)
        v_music.music = nullptr;
    Mix_FreeMusic(ptr);
}


bool sfx_init()
{
    Mix_OpenAudio(44100, AUDIO_S16, 2, 2048);
    Mix_AllocateChannels(sfxSound::k_channels);

    Mix_ChannelFinished(&sfxSound::onChannelFinished);
    Mix_HookMusicFinished(&musicfinished);

#ifndef __EMSCRIPTEN__
    const SDL_version* link_version = Mix_Linked_Version();
    printf("[sfx] SDL_Mixer %d.%d.%d initialized.\n",
        link_version->major, link_version->minor, link_version->patch);
#else
    SDL_version ver_compiled;
    SDL_MIXER_VERSION(&ver_compiled);
    printf("[sfx] SDL_Mixer %d.%d.%d initialized.\n",
        ver_compiled.major, ver_compiled.minor, ver_compiled.patch);
#endif

    return true;
}

void sfx_close()
{
    Mix_CloseAudio();
}

void sfx_stopallsounds()
{
    logEvent("S haltall");
    mixHaltChannel(-1);
}

void sfx_setmusicvolume(int volume)
{
    logEvent(std::format("S musicvolume {}", volume));
    Mix_VolumeMusic(volume);
}

void sfx_setsoundvolume(int volume)
{
    logEvent(std::format("S soundvolume {}", volume));
    Mix_Volume(-1, volume);
}

bool sfx_canPlayAudio()
{
#ifdef __EMSCRIPTEN__  // emscripten has sound capabilities
    return true;
#else
    int frequency, channels;
    Uint16 format;
    return Mix_QuerySpec(&frequency, &format, &channels) != 0 /* error */;
#endif
}

sfxSound::sfxSound(const fs::path& path)
{
    const std::string path_str = path.generic_string();
    std::cout << "loading " << path_str << " ...";

    m_sfx = MixChunkPtr(Mix_LoadWAV(path_str.c_str()));
    if (!m_sfx)
        throw std::format("Failed to load {}: {}", path_str, Mix_GetError());
    m_name = dataRelative(path_str);

    std::cout << " done" << std::endl;
}

bool sfxSound::play()
{
    const Uint32 current_time = sfx_ticks();
    if (current_time - m_last_start_time < 40) {
        logEvent(std::format("S skip {}", m_name));
        return false;
    }

    const int channel = mixPlayChannel(m_sfx.get(), 0);
    logEvent(std::format("S play {} ch={}", m_name, channel));
    if (channel < 0) {
        if (sfx_ignore_channel_failure)
            m_last_start_time = current_time;
        return sfx_ignore_channel_failure;
    }

    m_last_start_time = current_time;
    m_channels.set(channel);
    s_channels[channel] = this;
    return true;
}

void sfxSound::playLoop(int loops)
{
    const int channel = mixPlayChannel(m_sfx.get(), loops);
    logEvent(std::format("S loop {} loops={} ch={}", m_name, loops, channel));
    if (channel < 0)
        return;

    m_channels.set(channel);
}

void sfxSound::stop()
{
    logEvent(std::format("S stop {}", m_name));
    for (size_t i = 0; i < m_channels.size(); i++) {
        if (m_channels.test(i)) {
            mixHaltChannel(i);
        }
    }
}

void sfxSound::onChannelFinished(int channel)
{
    sfxSound* const sfx = s_channels[channel];
    if (sfx) {
        sfx->m_channels.reset(channel);
    }
    s_channels[channel] = nullptr;
}


sfxMusic::sfxMusic(const fs::path& path)
{
    const std::string path_str = path.generic_string();
    std::cout << "loading " << path_str << " ...";

    m_music = MixMusicPtr(Mix_LoadMUS(path_str.c_str()));
    if (!m_music)
        throw std::format("Failed to load {}: {}", path_str, Mix_GetError());
    m_name = dataRelative(path_str);

    std::cout << " done" << std::endl;
}

void sfxMusic::play(bool fPlayonce, bool fResume)
{
    logEvent(std::format("S music {} once={} resume={}", m_name, fPlayonce ? 1 : 0, fResume ? 1 : 0));
    if (sfx_virtual_mixer) {
        const double duration = Mix_MusicDuration(m_music.get());
        v_music.music = m_music.get();
        v_music.forever = !fPlayonce || duration <= 0.0;
        v_music.paused = false;
        v_music.end = sfx_ticks() + (Uint32)(duration * 1000.0);
    } else {
        Mix_PlayMusic(m_music.get(), fPlayonce ? 0 : -1);
    }
    fResumeMusic = fResume;
}

void sfxMusic::stop()
{
    logEvent(std::format("S musicstop {}", m_name));
    if (sfx_virtual_mixer)
        v_music.music = nullptr;
    else
        Mix_HaltMusic();
}

void sfxMusic::togglePause()
{
    logEvent(std::format("S musicpause {} paused={}", m_name, m_paused ? 0 : 1));
    if (sfx_virtual_mixer) {
        if (v_music.music) {
            if (m_paused && v_music.paused) {
                v_music.end = sfx_ticks() + v_music.remaining;
                v_music.paused = false;
            } else if (!m_paused && !v_music.paused) {
                v_music.remaining = v_music.end - sfx_ticks();
                v_music.paused = true;
            }
        }
    } else if (m_paused) {
        Mix_ResumeMusic();
    } else {
        Mix_PauseMusic();
    }
    m_paused = !m_paused;
}

bool sfxMusic::isPlaying() const
{
    if (sfx_virtual_mixer)
        return v_music.music != nullptr;
    return Mix_PlayingMusic();
}
