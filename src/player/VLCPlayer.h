#pragma once
// VLCPlayer.h - libVLC embedded player
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <atomic>
#include <vlc/vlc.h>

enum class PlayerState { Idle, Loading, Playing, Paused, Stopped, Error };

class VLCPlayer {
public:
    VLCPlayer();
    ~VLCPlayer();

    bool Init(HWND videoHwnd);
    bool Open(const std::string& utf8Url);
    void Play();
    void Pause();
    void Stop();
    void Close();
    void SeekTo(double seconds);
    void SetVolume(int vol0to100);

    // Disable/enable the video track without stopping audio (used on minimize)
    void SetVideoEnabled(bool enabled);

    double      GetPosition()  const;
    double      GetDuration()  const;
    float       GetBufferPct() const;   // 0..1 buffer fill
    PlayerState GetState()     const;   // polls VLC directly each call
    std::string GetError()     const { return m_error; }

    libvlc_media_player_t* GetMP() const { return m_mp; }

private:
    libvlc_instance_t*      m_vlc    = nullptr;
    libvlc_media_player_t*  m_mp     = nullptr;
    libvlc_media_t*         m_media  = nullptr;
    libvlc_event_manager_t* m_em     = nullptr;
    HWND                    m_hwnd   = NULL;
    std::string             m_error;
    int                     m_savedVideoTrack = 0;

    std::atomic<float> m_position { 0.0f };
    std::atomic<float> m_duration { 0.0f };
    std::atomic<float> m_bufferPct{ 0.0f }; // filled by Buffering event

    void AttachEvents();
    void DetachEvents();
    void ReleaseMedia();
    void TearDown();

    static void OnEvent(const libvlc_event_t* ev, void* ud);
};
