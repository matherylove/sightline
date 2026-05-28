#pragma once
// PersistentData — subscriptions, playlists, download queue, history
// All data is persisted to clienttube.ini via Config::GetString/SetString.
// Each list is stored as a semicolon-separated string under its section.

#include "Config.h"
#include <string>
#include <vector>
#include <algorithm>

namespace PersistentData {

// ---------------------------------------------------------------------------
// Generic list helpers (stored as "id1|name1;id2|name2;...")
// ---------------------------------------------------------------------------
struct NamedItem { std::string id, name; };

inline std::vector<NamedItem> LoadList(const wchar_t* section, const wchar_t* key) {
    std::wstring raw = Config::GetString(section, key, L"");
    std::vector<NamedItem> out;
    if (raw.empty()) return out;
    // convert to narrow
    int n = WideCharToMultiByte(CP_UTF8, 0, raw.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, raw.c_str(), -1, &s[0], n, NULL, NULL);

    size_t pos = 0;
    while (pos < s.size()) {
        size_t semi = s.find(';', pos);
        std::string token = s.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        size_t pipe = token.find('|');
        if (pipe != std::string::npos)
            out.push_back({ token.substr(0, pipe), token.substr(pipe + 1) });
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return out;
}

inline void SaveList(const wchar_t* section, const wchar_t* key, const std::vector<NamedItem>& list) {
    std::string s;
    for (auto& item : list) { if (!s.empty()) s += ';'; s += item.id + '|' + item.name; }
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    Config::SetString(section, key, w.c_str());
}

// ---------------------------------------------------------------------------
// Subscriptions
// ---------------------------------------------------------------------------
inline std::vector<NamedItem> GetSubscriptions() {
    return LoadList(L"Subscriptions", L"channels");
}
inline bool IsSubscribed(const std::string& channelId) {
    auto list = GetSubscriptions();
    for (auto& c : list) if (c.id == channelId) return true;
    return false;
}
inline void Subscribe(const std::string& channelId, const std::string& channelName) {
    auto list = GetSubscriptions();
    for (auto& c : list) if (c.id == channelId) return; // already
    list.push_back({ channelId, channelName });
    SaveList(L"Subscriptions", L"channels", list);
}
inline void Unsubscribe(const std::string& channelId) {
    auto list = GetSubscriptions();
    list.erase(std::remove_if(list.begin(), list.end(),
        [&](const NamedItem& c) { return c.id == channelId; }), list.end());
    SaveList(L"Subscriptions", L"channels", list);
}

// ---------------------------------------------------------------------------
// Playlists  (only "Favorites" for now; extendable)
// ---------------------------------------------------------------------------
inline std::vector<NamedItem> GetPlaylist(const char* playlistId) {
    int n = MultiByteToWideChar(CP_UTF8, 0, playlistId, -1, NULL, 0);
    std::wstring wpl(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, playlistId, -1, &wpl[0], n);
    return LoadList(L"Playlists", wpl.c_str());
}
inline bool IsInPlaylist(const char* playlistId, const std::string& videoId) {
    auto list = GetPlaylist(playlistId);
    for (auto& v : list) if (v.id == videoId) return true;
    return false;
}
inline void AddToPlaylist(const char* playlistId, const std::string& videoId, const std::string& title) {
    auto list = GetPlaylist(playlistId);
    for (auto& v : list) if (v.id == videoId) return;
    list.push_back({ videoId, title });
    int n = MultiByteToWideChar(CP_UTF8, 0, playlistId, -1, NULL, 0);
    std::wstring wpl(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, playlistId, -1, &wpl[0], n);
    SaveList(L"Playlists", wpl.c_str(), list);
}
inline void RemoveFromPlaylist(const char* playlistId, const std::string& videoId) {
    auto list = GetPlaylist(playlistId);
    list.erase(std::remove_if(list.begin(), list.end(),
        [&](const NamedItem& v) { return v.id == videoId; }), list.end());
    int n = MultiByteToWideChar(CP_UTF8, 0, playlistId, -1, NULL, 0);
    std::wstring wpl(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, playlistId, -1, &wpl[0], n);
    SaveList(L"Playlists", wpl.c_str(), list);
}

// ---------------------------------------------------------------------------
// Downloads  (queue persisted as "videoId|title" entries)
// ---------------------------------------------------------------------------
inline std::vector<NamedItem> GetDownloadQueue() {
    return LoadList(L"Downloads", L"queue");
}
inline void EnqueueDownload(const std::string& videoId, const std::string& title) {
    auto q = GetDownloadQueue();
    for (auto& v : q) if (v.id == videoId) return;
    q.push_back({ videoId, title });
    SaveList(L"Downloads", L"queue", q);
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
inline void PushHistory(const std::string& videoId, const std::string& title) {
    auto list = LoadList(L"History", L"watched");
    list.erase(std::remove_if(list.begin(), list.end(),
        [&](const NamedItem& v) { return v.id == videoId; }), list.end());
    list.insert(list.begin(), { videoId, title });
    if (list.size() > 200) list.resize(200);
    SaveList(L"History", L"watched", list);
}

} // namespace PersistentData
