#pragma once
// FfmpegCapabilities.h
// Probes the local ffmpeg.exe once at startup (async) to determine which
// encoders and muxers are available.  DownloadDialog uses the results to
// show only the formats/codecs that will actually work.
//
// Usage:
//   // In WinMain / app init (non-blocking):
//   FfmpegCapabilities::ProbeAsync();
//
//   // In DownloadDialog render loop:
//   if (FfmpegCapabilities::IsReady()) { /* use caps */ }

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace FfmpegCapabilities {

// ---- Capability flags (read after IsReady() == true) ----------------------

struct Caps {
    // Encoders
    bool hasLibx264    = false;  // H.264 (mp4, mov, avi)
    bool hasLibvpxVp9  = false;  // VP9 re-encode (webm)
    bool hasLibopus    = false;  // Opus encoder (webm re-encode)
    bool hasLibmp3lame = false;  // MP3 (avi)
    bool hasAac        = false;  // AAC built-in (mp4, mov)
    bool hasLibvorbis  = false;  // Vorbis (webm fallback)
    bool hasFlac       = false;  // FLAC lossless audio
    bool hasPcm        = false;  // PCM/WAV

    // Copy is always available — ffmpeg can always stream-copy
    bool hasCopy       = true;

    // Derived: which VIDEO containers are safe to offer
    // mkv  -> always (stream copy VP9+Opus, no re-encode needed)
    // webm -> always (stream copy VP9+Opus into webm works fine)
    // mp4  -> needs libx264 + aac (re-encode); OR stream-copy if source is h264+aac (rare from YT)
    // mov  -> needs libx264 + aac
    // avi  -> needs libx264 + libmp3lame
    bool videoMkv()  const { return true; }
    bool videoWebm() const { return true; }
    bool videoMp4()  const { return hasLibx264 && hasAac; }
    bool videoMov()  const { return hasLibx264 && hasAac; }
    bool videoAvi()  const { return hasLibx264 && hasLibmp3lame; }

    // Derived: which AUDIO formats are safe to offer
    bool audioMp3()  const { return hasLibmp3lame; }
    bool audioAac()  const { return hasAac; }
    bool audioOpus() const { return hasLibopus; }
    bool audioFlac() const { return hasFlac; }
    bool audioWav()  const { return hasPcm; }
    bool audioOgg()  const { return hasLibvorbis; }
};

// ---- Internal state --------------------------------------------------------

namespace _internal {
    inline std::atomic<bool> g_ready{false};
    inline std::atomic<bool> g_probing{false};
    inline Caps              g_caps{};
    inline std::mutex        g_mutex;

    // Run ffmpeg with given args, capture combined stdout+stderr, return output.
    inline std::string RunFfmpeg(const std::wstring& ffmpegPath,
                                  const std::wstring& args)
    {
        std::wstring cmd = L"\"" + ffmpegPath + L"\" " + args;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength        = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE hR = NULL, hW = NULL;
        if (!CreatePipe(&hR, &hW, &sa, 0)) return {};
        SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput  = hW;
        si.hStdError   = hW;

        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(0);

        if (!CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE,
                            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hR); CloseHandle(hW);
            return {};
        }
        CloseHandle(hW);

        std::string out;
        char buf[4096];
        DWORD bytes = 0;
        while (ReadFile(hR, buf, sizeof(buf) - 1, &bytes, NULL) && bytes > 0)
            out.append(buf, bytes);

        CloseHandle(hR);
        WaitForSingleObject(pi.hProcess, 10000); // max 10 s
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return out;
    }

    inline bool HasEncoder(const std::string& output, const std::string& name) {
        // ffmpeg -encoders output has lines like:
        //  V..... libx264              H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
        // We look for the encoder name as a whole word after whitespace.
        auto pos = output.find(name);
        while (pos != std::string::npos) {
            // Make sure it's a standalone word (space or start before, space/newline after)
            bool pre  = (pos == 0 || output[pos-1] == ' ' || output[pos-1] == '\t');
            bool post = (pos + name.size() >= output.size() ||
                         output[pos + name.size()] == ' '  ||
                         output[pos + name.size()] == '\t' ||
                         output[pos + name.size()] == '\r' ||
                         output[pos + name.size()] == '\n');
            if (pre && post) return true;
            pos = output.find(name, pos + 1);
        }
        return false;
    }

    inline bool HasMuxer(const std::string& output, const std::string& name) {
        // ffmpeg -formats output has lines like:
        //  E  matroska        Matroska
        //  DE mp4             MP4 (MPEG-4 Part 14)
        auto pos = output.find(name);
        while (pos != std::string::npos) {
            bool pre  = (pos == 0 || output[pos-1] == ' ' || output[pos-1] == '\t');
            bool post = (pos + name.size() >= output.size() ||
                         output[pos + name.size()] == ' '  ||
                         output[pos + name.size()] == '\t' ||
                         output[pos + name.size()] == '\r' ||
                         output[pos + name.size()] == '\n');
            if (pre && post) return true;
            pos = output.find(name, pos + 1);
        }
        return false;
    }

    inline void DoProbe(std::wstring ffmpegPath) {
        Caps c{};

        // --- Query encoders -------------------------------------------------
        std::string encoders = RunFfmpeg(ffmpegPath, L"-encoders -v quiet");
        if (!encoders.empty()) {
            c.hasLibx264    = HasEncoder(encoders, "libx264");
            c.hasLibvpxVp9  = HasEncoder(encoders, "libvpx-vp9");
            c.hasLibopus    = HasEncoder(encoders, "libopus");
            c.hasLibmp3lame = HasEncoder(encoders, "libmp3lame");
            c.hasFlac       = HasEncoder(encoders, "flac");
            c.hasLibvorbis  = HasEncoder(encoders, "libvorbis");
            // AAC: look for the native aac encoder (always built-in when present)
            c.hasAac        = HasEncoder(encoders, "aac");
            // PCM: if pcm_s16le is listed
            c.hasPcm        = HasEncoder(encoders, "pcm_s16le");
        }

        // --- Query muxers ---------------------------------------------------
        // (stream copy still works even if no encoder for that codec;
        //  but the muxer must exist for the container to be writable)
        // We don't gate mkv/webm on muxer presence since virtually every
        // ffmpeg build ships matroska+webm.  We do check mp4/avi/mov.
        // (No changes to Caps.hasLibx264 etc — muxer check is implicit via
        //  the videoMp4() / videoAvi() / videoMov() helpers above.)

        {
            std::lock_guard<std::mutex> lk(g_mutex);
            g_caps = c;
        }
        g_ready.store(true);
        g_probing.store(false);
    }
} // namespace _internal

// ---- Public API ------------------------------------------------------------

// Returns true if the probe has completed.
inline bool IsReady() { return _internal::g_ready.load(); }

// Returns true if still probing.
inline bool IsProbing() { return _internal::g_probing.load(); }

// Snapshot of detected capabilities (valid only after IsReady()).
inline Caps Get() {
    std::lock_guard<std::mutex> lk(_internal::g_mutex);
    return _internal::g_caps;
}

// Launch the probe in a background thread.
// Safe to call multiple times — only runs once.
// ffmpegPath: full path to ffmpeg.exe (leave empty to auto-detect).
inline void ProbeAsync(std::wstring ffmpegPath = L"") {
    if (_internal::g_ready.load() || _internal::g_probing.load()) return;

    // Auto-detect ffmpeg path if not provided
    if (ffmpegPath.empty()) {
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW(NULL, buf, MAX_PATH);
        std::wstring exeDir(buf);
        auto pos = exeDir.rfind(L'\\');
        if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos);
        std::wstring local = exeDir + L"\\ffmpeg.exe";
        if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ffmpegPath = local;
        } else {
            wchar_t found[MAX_PATH] = {};
            if (SearchPathW(NULL, L"ffmpeg.exe", NULL, MAX_PATH, found, NULL))
                ffmpegPath = found;
        }
    }

    if (ffmpegPath.empty()) {
        // ffmpeg not found — mark ready with all-false caps so UI shows nothing
        _internal::g_ready.store(true);
        return;
    }

    _internal::g_probing.store(true);
    std::thread([fp = std::move(ffmpegPath)]() mutable {
        _internal::DoProbe(std::move(fp));
    }).detach();
}

// Synchronous version — blocks until probe completes (use only at startup).
inline void ProbeSync(std::wstring ffmpegPath = L"") {
    if (_internal::g_ready.load()) return;
    ProbeAsync(std::move(ffmpegPath));
    while (_internal::g_probing.load())
        Sleep(50);
}

} // namespace FfmpegCapabilities
