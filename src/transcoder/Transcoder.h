#pragma once
// Transcoder.h
// Converts a video/audio source to a target format using FFmpeg.
//
// Container / codec matrix (explains why "-c copy" is gone):
//
//   MP4   video: H.264 (HW when available)    audio: AAC
//         VP9+Opus in MP4 is "experimental" in FFmpeg -> always transcode.
//
//   WEBM  video: libvpx-vp9 (pass-through if source is already VP9)
//         audio: libopus
//         WebM only supports VP8/VP9 video + Vorbis/Opus audio.
//         Any other source codec must be transcoded first.
//
//   MKV   video: copy (MKV accepts virtually any codec)
//         audio: copy
//         Used for lossless mux of VP9+Opus without re-encoding.
//
//   WAV   audio only, pcm_s16le, lossless
//   MP3   audio only, libmp3lame 192 kbps
//   AAC   audio only, aac 192 kbps
//
// Metadata is written via -metadata for all formats.
// Hardware decode is attempted for MP4 only (not worth the complexity for others).

#include "GpuDetector.h"
#include "FfmpegRunner.h"
#include <string>

namespace Transcoder {

enum class Format { MP3, AAC, MP4, WEBM, MKV, WAV };

struct ConvertResult {
    bool        ok;
    std::string error;
};

struct Metadata {
    std::wstring title;
    std::wstring artist;   // channel name
    std::wstring comment;  // short description
};

// inputPath  — full path to the muxed/demuxed source (or "vidPath\naudioPath")
// outputPath — full output path
// format     — target container/codec
// meta       — optional metadata to embed (all fields may be empty)
inline ConvertResult Convert(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    Format              format,
    const Metadata&     meta = {})
{
    static GpuDetector::HwProfile s_profile = GpuDetector::Detect();
    const auto& p = s_profile;

    // Split "video\naudio" dual-input paths
    std::wstring vidPath = inputPath;
    std::wstring audPath;
    size_t nl = inputPath.find(L'\n');
    if (nl != std::wstring::npos) {
        vidPath = inputPath.substr(0, nl);
        audPath = inputPath.substr(nl + 1);
    }

    auto Q = [](const std::wstring& s) { return L"\"" + s + L"\""; };

    std::vector<std::wstring> args;
    args.push_back(L"-y");  // overwrite without asking

    // Hardware decode only for MP4 (H.264 encode path)
    bool useHwDecode = p.hardwareAvailable && (format == Format::MP4);
    if (useHwDecode && !p.hwaccel.empty()) {
        args.push_back(L"-hwaccel");
        args.push_back(std::wstring(p.hwaccel.begin(), p.hwaccel.end()));
    }

    // Inputs
    args.push_back(L"-i"); args.push_back(vidPath);
    if (!audPath.empty()) {
        args.push_back(L"-i"); args.push_back(audPath);
    }

    // Metadata flags (added before codec flags so ffmpeg can write them into
    // the header; applies to all formats including MKV and WEBM)
    auto metaEsc = [](const std::wstring& s) {
        std::wstring r;
        for (wchar_t c : s) {
            if (c==L'='||c==L';'||c==L'#'||c==L'\n'||c==L'\\') r+=L'\\';
            r+=c;
        }
        return r;
    };
    bool hasMeta = !meta.title.empty() || !meta.artist.empty() || !meta.comment.empty();
    if (hasMeta) {
        if (!meta.title.empty())   { args.push_back(L"-metadata"); args.push_back(L"title="   + metaEsc(meta.title));   }
        if (!meta.artist.empty())  { args.push_back(L"-metadata"); args.push_back(L"artist="  + metaEsc(meta.artist));  }
        if (!meta.comment.empty()) { args.push_back(L"-metadata"); args.push_back(L"comment=" + metaEsc(meta.comment)); }
    }

    switch (format) {
    // ------------------------------------------------------------------
    case Format::MP3:
        args.push_back(L"-vn");
        args.push_back(L"-acodec"); args.push_back(L"libmp3lame");
        args.push_back(L"-ab");    args.push_back(L"192k");
        args.push_back(L"-ar");    args.push_back(L"44100");
        break;

    // ------------------------------------------------------------------
    case Format::AAC:
        args.push_back(L"-vn");
        args.push_back(L"-acodec"); args.push_back(L"aac");
        args.push_back(L"-ab");    args.push_back(L"192k");
        break;

    // ------------------------------------------------------------------
    // MP4: H.264 video + AAC audio.
    // VP9+Opus in MP4 is experimental in FFmpeg and causes
    // "could not write header" errors on many builds — always transcode.
    case Format::MP4: {
        std::wstring venc(p.encH264.begin(), p.encH264.end());
        args.push_back(L"-c:v"); args.push_back(venc);
        if (p.vendor == GpuDetector::Vendor::NVIDIA) {
            args.push_back(L"-rc"); args.push_back(L"vbr");
            args.push_back(L"-cq"); args.push_back(L"23");
        } else if (p.vendor == GpuDetector::Vendor::AMD) {
            args.push_back(L"-quality"); args.push_back(L"balanced");
        } else if (p.vendor == GpuDetector::Vendor::Intel) {
            args.push_back(L"-global_quality"); args.push_back(L"23");
        } else {
            args.push_back(L"-crf");    args.push_back(L"23");
            args.push_back(L"-preset"); args.push_back(L"fast");
        }
        args.push_back(L"-c:a");     args.push_back(L"aac");
        args.push_back(L"-b:a");     args.push_back(L"192k");
        args.push_back(L"-movflags"); args.push_back(L"+faststart");
        break;
    }

    // ------------------------------------------------------------------
    // WEBM: VP9 video + Opus audio.
    // WebM ONLY accepts VP8/VP9 + Vorbis/Opus — any other codec will
    // error.  We always re-encode to be safe.
    case Format::WEBM:
        args.push_back(L"-c:v"); args.push_back(L"libvpx-vp9");
        args.push_back(L"-crf"); args.push_back(L"30");
        args.push_back(L"-b:v"); args.push_back(L"0");
        args.push_back(L"-c:a"); args.push_back(L"libopus");
        args.push_back(L"-b:a"); args.push_back(L"128k");
        break;

    // ------------------------------------------------------------------
    // MKV: stream-copy for lossless mux of VP9+Opus (or any source codec).
    // MKV accepts virtually any codec so -c copy always works here.
    case Format::MKV:
        args.push_back(L"-c:v"); args.push_back(L"copy");
        args.push_back(L"-c:a"); args.push_back(L"copy");
        break;

    // ------------------------------------------------------------------
    case Format::WAV:
        args.push_back(L"-vn");
        args.push_back(L"-acodec"); args.push_back(L"pcm_s16le");
        break;
    }

    args.push_back(outputPath);

    auto res = FfmpegRunner::Run(args);

    // If HW encode failed, retry with software libx264
    if (!res.ok && useHwDecode) {
        args.clear();
        args.push_back(L"-y");
        args.push_back(L"-i"); args.push_back(vidPath);
        if (!audPath.empty()) { args.push_back(L"-i"); args.push_back(audPath); }
        if (hasMeta) {
            if (!meta.title.empty())   { args.push_back(L"-metadata"); args.push_back(L"title="   + metaEsc(meta.title));   }
            if (!meta.artist.empty())  { args.push_back(L"-metadata"); args.push_back(L"artist="  + metaEsc(meta.artist));  }
            if (!meta.comment.empty()) { args.push_back(L"-metadata"); args.push_back(L"comment=" + metaEsc(meta.comment)); }
        }
        args.push_back(L"-c:v");     args.push_back(L"libx264");
        args.push_back(L"-crf");     args.push_back(L"23");
        args.push_back(L"-preset");  args.push_back(L"fast");
        args.push_back(L"-c:a");     args.push_back(L"aac");
        args.push_back(L"-b:a");     args.push_back(L"192k");
        args.push_back(L"-movflags"); args.push_back(L"+faststart");
        args.push_back(outputPath);
        res = FfmpegRunner::Run(args);
    }

    ConvertResult cr;
    cr.ok    = res.ok;
    cr.error = res.error.empty()
               ? (res.ok ? "" : "FFmpeg exited with code " + std::to_string(res.exitCode))
               : res.error;
    return cr;
}

// Convenience wrappers (no metadata)
inline ConvertResult ToMp3 (const std::wstring& in, const std::wstring& out) { return Convert(in, out, Format::MP3);  }
inline ConvertResult ToMp4 (const std::wstring& in, const std::wstring& out) { return Convert(in, out, Format::MP4);  }
inline ConvertResult ToWebm(const std::wstring& in, const std::wstring& out) { return Convert(in, out, Format::WEBM); }
inline ConvertResult ToMkv (const std::wstring& in, const std::wstring& out) { return Convert(in, out, Format::MKV);  }
inline ConvertResult ToAac (const std::wstring& in, const std::wstring& out) { return Convert(in, out, Format::AAC);  }
inline ConvertResult ToWav (const std::wstring& in, const std::wstring& out) { return Convert(in, out, Format::WAV);  }

} // namespace Transcoder
