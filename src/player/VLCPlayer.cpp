// VLCPlayer.cpp
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "VLCPlayer.h"

VLCPlayer::VLCPlayer()  {}
VLCPlayer::~VLCPlayer() { TearDown(); }

void VLCPlayer::ReleaseMedia() {
    DetachEvents();
    if (m_mp)    { libvlc_media_player_stop(m_mp);    libvlc_media_player_release(m_mp);    m_mp    = nullptr; }
    if (m_media) {                                     libvlc_media_release(m_media);         m_media = nullptr; }
    m_position.store(0.0f);
    m_duration.store(0.0f);
    m_bufferPct.store(0.0f);
}

void VLCPlayer::TearDown() {
    ReleaseMedia();
    if (m_vlc) { libvlc_release(m_vlc); m_vlc = nullptr; }
}

bool VLCPlayer::Init(HWND hwnd) {
    m_hwnd = hwnd;
    const char* args[] = {
        "--no-video-title-show",
        "--no-xlib",
        "--quiet",
        "--vout=direct3d9",
        "--no-embedded-video",
    };
    m_vlc = libvlc_new(5, args);
    if (!m_vlc) { m_error = "libvlc_new() failed - libvlc.dll missing?"; return false; }
    return true;
}

void VLCPlayer::AttachEvents() {
    if (!m_mp) return;
    m_em = libvlc_media_player_event_manager(m_mp);
    libvlc_event_attach(m_em, libvlc_MediaPlayerPlaying,         OnEvent, this);
    libvlc_event_attach(m_em, libvlc_MediaPlayerPaused,          OnEvent, this);
    libvlc_event_attach(m_em, libvlc_MediaPlayerStopped,         OnEvent, this);
    libvlc_event_attach(m_em, libvlc_MediaPlayerEncounteredError,OnEvent, this);
    libvlc_event_attach(m_em, libvlc_MediaPlayerEndReached,      OnEvent, this);
    libvlc_event_attach(m_em, libvlc_MediaPlayerPositionChanged, OnEvent, this);
    libvlc_event_attach(m_em, libvlc_MediaPlayerLengthChanged,   OnEvent, this);
    libvlc_event_attach(m_em, libvlc_MediaPlayerBuffering,       OnEvent, this);
}

void VLCPlayer::DetachEvents() {
    if (!m_em) return;
    libvlc_event_detach(m_em, libvlc_MediaPlayerPlaying,         OnEvent, this);
    libvlc_event_detach(m_em, libvlc_MediaPlayerPaused,          OnEvent, this);
    libvlc_event_detach(m_em, libvlc_MediaPlayerStopped,         OnEvent, this);
    libvlc_event_detach(m_em, libvlc_MediaPlayerEncounteredError,OnEvent, this);
    libvlc_event_detach(m_em, libvlc_MediaPlayerEndReached,      OnEvent, this);
    libvlc_event_detach(m_em, libvlc_MediaPlayerPositionChanged, OnEvent, this);
    libvlc_event_detach(m_em, libvlc_MediaPlayerLengthChanged,   OnEvent, this);
    libvlc_event_detach(m_em, libvlc_MediaPlayerBuffering,       OnEvent, this);
    m_em = nullptr;
}

void VLCPlayer::OnEvent(const libvlc_event_t* ev, void* ud) {
    VLCPlayer* self = (VLCPlayer*)ud;
    switch (ev->type) {
        case libvlc_MediaPlayerPlaying:
            if (self->m_mp) {
                libvlc_time_t ms = libvlc_media_player_get_length(self->m_mp);
                if (ms > 0) self->m_duration.store((float)(ms / 1000.0));
            }
            break;
        case libvlc_MediaPlayerLengthChanged: {
            libvlc_time_t ms = ev->u.media_player_length_changed.new_length;
            if (ms > 0) self->m_duration.store((float)(ms / 1000.0));
            break;
        }
        case libvlc_MediaPlayerPositionChanged:
            self->m_position.store(ev->u.media_player_position_changed.new_position);
            break;
        case libvlc_MediaPlayerBuffering:
            // new_cache is 0..100
            self->m_bufferPct.store(ev->u.media_player_buffering.new_cache / 100.f);
            break;
        case libvlc_MediaPlayerEncounteredError:
            self->m_error = "Error de reproduccion";
            break;
        default: break;
    }
}

bool VLCPlayer::Open(const std::string& utf8Url) {
    if (!m_vlc) { m_error = "Player not initialized"; return false; }
    ReleaseMedia();
    m_error.clear();

    std::string videoUrl = utf8Url, audioUrl;
    size_t nlPos = utf8Url.find('\n');
    if (nlPos != std::string::npos) {
        videoUrl = utf8Url.substr(0, nlPos);
        audioUrl = utf8Url.substr(nlPos + 1);
        while (!audioUrl.empty() && (audioUrl.back()=='\r'||audioUrl.back()=='\n'||audioUrl.back()==' '))
            audioUrl.pop_back();
    }

    bool isUrl = videoUrl.find("http://")==0 || videoUrl.find("https://")==0 || videoUrl.find("rtsp://")==0;
    m_media = isUrl
        ? libvlc_media_new_location(m_vlc, videoUrl.c_str())
        : libvlc_media_new_path   (m_vlc, videoUrl.c_str());
    if (!m_media) { m_error = "libvlc_media_new failed: " + videoUrl; return false; }

    if (!audioUrl.empty())
        libvlc_media_add_option(m_media, (":input-slave=" + audioUrl).c_str());

    m_mp = libvlc_media_player_new_from_media(m_media);
    if (!m_mp) { m_error = "libvlc_media_player_new failed"; return false; }

    libvlc_media_player_set_hwnd(m_mp, (void*)m_hwnd);
    AttachEvents();

    if (libvlc_media_player_play(m_mp) != 0) {
        m_error = "libvlc_media_player_play() error";
        return false;
    }
    return true;
}

void VLCPlayer::Play()  { if (m_mp) libvlc_media_player_play(m_mp); }
void VLCPlayer::Pause() { if (m_mp && libvlc_media_player_can_pause(m_mp)) libvlc_media_player_pause(m_mp); }
void VLCPlayer::Stop()  { if (m_mp) libvlc_media_player_stop(m_mp); }
void VLCPlayer::Close() { ReleaseMedia(); }

void VLCPlayer::SeekTo(double seconds) {
    if (!m_mp) return;
    double dur = (double)m_duration.load();
    if (dur <= 0.0) return;
    float frac = (float)(seconds / dur);
    if (frac < 0.f) frac = 0.f;
    if (frac > 1.f) frac = 1.f;
    libvlc_media_player_set_position(m_mp, frac);
}

void VLCPlayer::SetVolume(int v) {
    if (m_mp) libvlc_audio_set_volume(m_mp, v);
}

double VLCPlayer::GetPosition() const {
    double dur = (double)m_duration.load();
    return dur > 0.0 ? (double)m_position.load() * dur : 0.0;
}

double VLCPlayer::GetDuration() const { return (double)m_duration.load(); }

float VLCPlayer::GetBufferPct() const { return m_bufferPct.load(); }

// GetState: polls libvlc directly - never stale regardless of event delivery.
PlayerState VLCPlayer::GetState() const {
    if (!m_mp) return PlayerState::Idle;
    switch (libvlc_media_player_get_state(m_mp)) {
        case libvlc_Opening:
        case libvlc_Buffering: return PlayerState::Loading;
        case libvlc_Playing:   return PlayerState::Playing;
        case libvlc_Paused:    return PlayerState::Paused;
        case libvlc_Stopped:
        case libvlc_Ended:     return PlayerState::Stopped;
        case libvlc_Error:     return PlayerState::Error;
        default:               return PlayerState::Idle;
    }
}
