#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <vector>
#include "../extractor/InnerTube.h"

enum class AppTab {
    What_sNew = 0,
    Trending,
    Subscriptions,
    Bookmarks,
    History,
    Downloads,
    COUNT
};

enum class AppPage {
    Main = 0,
    VideoDetail,
    Channel,
    Playlist,
    Search,
    Settings,
    About
};

struct PlayerRequest {
    std::string videoUrl;
    std::string audioUrl;
    bool        isDash = false;

    std::string title;
    std::string channelName;
    std::string channelId;
    std::string duration;
    std::string viewCount;
    std::string description;
    std::string videoId;
    std::string thumbnailUrl;

    // VP9 quality list
    std::vector<VP9Quality> vp9Qualities;
    bool vp9QualitiesReady   = false;
    bool vp9QualitiesLoading = false;
};

// Async comments result delivered from the background thread.
struct PendingComments {
    std::string              videoId;            // which video these belong to
    std::vector<CommentItem> comments;           // accumulated list
    std::string              continuationToken;  // for next page
    std::string              error;              // non-empty on failure
    bool                     ready    = false;   // page result is ready to consume
    bool                     loading  = false;   // background thread running
    bool                     loadMore = false;   // UI requested next page
};

// Async related-videos result delivered from the background thread.
struct PendingRelated {
    std::string              videoId;            // which video these belong to
    std::vector<RelatedItem> items;              // results
    std::string              error;              // non-empty on failure
    bool                     ready   = false;
    bool                     loading = false;
    // The thread handle is written here by VD_RequestRelated so that
    // MainWindow::DrawContent can absorb it into m_hRelatedThread and
    // properly WaitForSingleObject/CloseHandle on application exit.
    HANDLE                   hThread = NULL;
};

struct AppState {
    AppTab  activeTab       = AppTab::What_sNew;
    AppPage activePage      = AppPage::Main;
    AppPage prevPage        = AppPage::Main;
    bool    drawerOpen      = false;
    bool    miniPlayer      = false;
    char    searchBuf[512]  = {};
    int     selectedVideo   = -1;
    int     selectedChannel = -1;

    bool          playRequested  = false;
    std::atomic<bool> streamResolving{false};

    PlayerRequest pendingPlay;

    // Comments async state
    PendingComments pendingComments;
    std::atomic<bool> commentsResolving{false};

    // Related videos async state (Up Next panel)
    PendingRelated pendingRelated;
    std::atomic<bool> relatedResolving{false};

    std::string pendingChannelId;
    std::string pendingChannelName;

    bool debugConsole = false;
};
