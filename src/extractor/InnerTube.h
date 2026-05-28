#pragma once
#include <string>
#include <vector>
#include <map>
#include "../ui/VideoList.h"

struct VideoInfo {
    std::string description;
    std::string channelId;
    std::string viewCount;
    std::string likeCount;
};

// Represents one VP9 adaptive stream paired with the best opus audio track.
struct VP9Quality {
    std::string label;      // e.g. "1080p", "720p60"
    std::string videoUrl;   // VP9 video-only stream URL
    std::string audioUrl;   // Best opus/m4a audio-only stream URL
    int         height = 0; // Video height in pixels
};

// Represents one YouTube comment (top-level only).
struct CommentItem {
    std::string authorName;
    std::string text;
    std::string likeCount;   // formatted string, e.g. "1.2K"
    std::string publishedAt; // e.g. "2 days ago"
    bool        isAuthor = false; // true if comment is from the video author
};

// Result of a GetComments call.
struct CommentsPage {
    std::vector<CommentItem> comments;
    std::string              continuationToken; // non-empty if more pages exist
    std::string              error;             // non-empty on failure
};

// One entry in the "Up Next" / related videos panel.
struct RelatedItem {
    std::string videoId;      // empty for playlists/radios
    std::string playlistId;   // empty for plain videos
    std::string title;
    std::string channelName;
    std::string duration;     // formatted, e.g. "4:32"  (empty for live)
    std::string viewCount;    // formatted string
    std::string publishedAt;  // e.g. "2 days ago" (from lockupViewModel metadata)
    std::string thumbnailUrl;
    bool        isPlaylist = false;
};

namespace InnerTube {
    std::vector<VideoItem>   Search(const std::string& query,
                                    std::string* errorOut = nullptr);
    std::string              GetStreamUrl(const std::string& videoId);
    VideoInfo                GetVideoInfo(const std::string& videoId);
    // Returns all available VP9 qualities, sorted best-first.
    std::vector<VP9Quality>  GetVP9Qualities(const std::string& videoId);

    // Fetches one page of top-level comments for videoId.
    CommentsPage             GetComments(const std::string& videoId,
                                         const std::string& continuationToken = "");

    // Fetches the "Up Next" related videos for videoId using /youtubei/v1/next.
    // Returns an empty vector on failure.
    std::vector<RelatedItem> GetRelatedVideos(const std::string& videoId);

    std::string              Post(const std::string& endpoint,
                                  const std::string& jsonBody,
                                  const char* clientName    = nullptr,
                                  const char* clientVersion = nullptr);
}
