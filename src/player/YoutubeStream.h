#pragma once
// YoutubeStream.h
// Resolves a YouTube video ID into a direct stream URL via InnerTube.
// Calls /youtubei/v1/player (ANDROID client), picks itag 18 (360p muxed H.264+AAC)
// which is the safest for XP DirectShow and libVLC without DASH.

#include <string>
#include "../extractor/InnerTube.h"

struct StreamInfo {
    std::string videoUrl;  // direct HTTPS stream URL
    std::string audioUrl;  // separate audio URL (DASH only; empty if muxed)
    int         width  = 0;
    int         height = 0;
    std::string codec;
    bool        isDash = false;
};

namespace YoutubeStream {

inline std::string ExtractVideoId(const std::string& watchUrl) {
    auto pos = watchUrl.find("v=");
    if (pos == std::string::npos) return watchUrl;
    std::string id = watchUrl.substr(pos + 2);
    auto amp = id.find('&');
    if (amp != std::string::npos) id = id.substr(0, amp);
    return id;
}

// Resolves videoId -> direct stream URL using InnerTube::GetStreamUrl.
// Returns an empty videoUrl on failure.
inline StreamInfo Resolve(const std::string& videoId) {
    StreamInfo info;
    info.videoUrl = InnerTube::GetStreamUrl(videoId);
    info.isDash   = false;
    return info;
}

} // namespace YoutubeStream
