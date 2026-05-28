#include "InnerTube.h"
#include "SigDecipher.h"
#include "../ui/CTLogger.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "nlohmann/json.hpp"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

#define DIAG(fmt, ...) do { \
    char _dbuf[512]; \
    snprintf(_dbuf, sizeof(_dbuf), "[CT] " fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(_dbuf); \
    CTLogger::LogC('I', fmt, ##__VA_ARGS__); \
} while(0)

// ---------------------------------------------------------------------------
// Client contexts
// ---------------------------------------------------------------------------
static const std::string kCtxWeb = R"({
  "context": { "client": {
    "clientName": "WEB",
    "clientVersion": "2.20240101.00.00",
    "hl": "en", "gl": "US"
  }}
})";

static const std::string kCtxWebEmbed = R"({
  "context": {
    "client": {
      "clientName": "WEB_EMBEDDED_PLAYER",
      "clientVersion": "2.20231219.01.00",
      "hl": "en", "gl": "US"
    },
    "thirdParty": {
      "embedUrl": "https://www.youtube.com"
    }
  }
})";

static const std::string kCtxTV = R"({
  "context": {
    "client": {
      "clientName": "TVHTML5_SIMPLY_EMBEDDED_PLAYER",
      "clientVersion": "2.0",
      "hl": "en", "gl": "US"
    },
    "thirdParty": {
      "embedUrl": "https://www.youtube.com"
    }
  }
})";

static int ClientNameToId(const char* name) {
    if (!strcmp(name, "WEB"))                             return 1;
    if (!strcmp(name, "WEB_EMBEDDED_PLAYER"))             return 56;
    if (!strcmp(name, "TVHTML5_SIMPLY_EMBEDDED_PLAYER"))  return 85;
    return 1;
}

// ---------------------------------------------------------------------------
// OS version helper
// ---------------------------------------------------------------------------
struct CT_OsVer { DWORD maj, min; };

static CT_OsVer CT_GetOsVersion() {
    typedef LONG (WINAPI* RtlGetVersionFn)(OSVERSIONINFOEXW*);
    OSVERSIONINFOEXW ovi = {};
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(hNtdll, "RtlGetVersion");
        if (fn) fn(&ovi);
    }
    CT_OsVer v = { ovi.dwMajorVersion, ovi.dwMinorVersion };
    return v;
}

static bool CT_IsPreWin8() {
    CT_OsVer v = CT_GetOsVersion();
    return (v.maj < 6) || (v.maj == 6 && v.min < 2);
}

// ---------------------------------------------------------------------------
// WinHTTP POST
// ---------------------------------------------------------------------------
std::string InnerTube::Post(const std::string& endpoint,
                             const std::string& body,
                             const char* clientName,
                             const char* clientVersion) {
    std::string result;
    DIAG("[InnerTube] Post: %s client=%s", endpoint.c_str(), clientName ? clientName : "?");

    DWORD proxyType = CT_IsPreWin8()
        ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY
        : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;

    HINTERNET hSess = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        proxyType,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) {
        hSess = WinHttpOpen(
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!hSess) { DIAG("[InnerTube] WinHttpOpen failed: %lu", GetLastError()); return result; }

    DWORD secProto =
        WINHTTP_FLAG_SECURE_PROTOCOL_SSL3   |
        WINHTTP_FLAG_SECURE_PROTOCOL_TLS1   |
        WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
        WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSess, WINHTTP_OPTION_SECURE_PROTOCOLS, &secProto, sizeof(secProto));
    WinHttpSetTimeouts(hSess, 15000, 15000, 30000, 30000);

    HINTERNET hConn = WinHttpConnect(hSess, L"www.youtube.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return result; }

    std::wstring wEp(endpoint.begin(), endpoint.end());
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", wEp.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result; }

    std::wstring hdrs =
        L"Content-Type: application/json\r\n"
        L"Accept: application/json\r\n"
        L"Origin: https://www.youtube.com\r\n"
        L"Referer: https://www.youtube.com/\r\n"
        L"X-Youtube-Bootstrap-Logged-In: false\r\n";
    if (clientName && clientVersion) {
        wchar_t buf[256];
        swprintf(buf, 256, L"X-YouTube-Client-Name: %d\r\n", ClientNameToId(clientName));
        hdrs += buf;
        int n = MultiByteToWideChar(CP_UTF8, 0, clientVersion, -1, NULL, 0);
        std::wstring wver(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, clientVersion, -1, &wver[0], n);
        if (!wver.empty() && wver.back() == L'\0') wver.pop_back();
        hdrs += L"X-YouTube-Client-Version: " + wver + L"\r\n";
    }
    WinHttpAddRequestHeaders(hReq, hdrs.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result;
    }
    if (!WinHttpReceiveResponse(hReq, NULL)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result;
    }

    DWORD status = 0, ssz = sizeof(DWORD);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL, &status, &ssz, NULL);
    DIAG("[InnerTube] HTTP %lu for %s", status, endpoint.c_str());

    if (status == 200) {
        DWORD dwRead = 0; char buf[8192];
        while (WinHttpReadData(hReq, buf, sizeof(buf)-1, &dwRead) && dwRead > 0)
            { buf[dwRead]='\0'; result+=buf; }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return result;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::wstring ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static std::string TextNode(const json& node) {
    if (node.is_null()) return "";
    if (node.contains("simpleText")) return node.value("simpleText", "");
    if (node.contains("runs") && !node["runs"].empty())
        return node["runs"][0].value("text", "");
    return "";
}

static std::string FindYtDlp() {
    CT_OsVer v = CT_GetOsVersion();
    const char* exeName;
    if (v.maj == 5)
        exeName = "yt-dlpxp.exe";
    else if (v.maj == 6 && v.min == 1)
        exeName = "yt-dlp7.exe";
    else
        exeName = "yt-dlp.exe";

    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        std::string dir(exePath);
        size_t sep = dir.find_last_of("\\/");
        if (sep != std::string::npos) dir = dir.substr(0, sep + 1);
        std::string candidate = dir + exeName;
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
    }
    std::string baseName(exeName, strlen(exeName) - 4);
    char buf[MAX_PATH] = {};
    if (SearchPathA(NULL, baseName.c_str(), ".exe", MAX_PATH, buf, NULL))
        return std::string(buf);
    return "";
}

static std::string ExtractUrlFromLine(const std::string& line) {
    if (line.find("http") == 0) return line;
    size_t rp = line.rfind(')');
    if (rp == std::string::npos) return "";
    size_t lp = line.rfind('(', rp);
    if (lp == std::string::npos) return "";
    std::string inner = line.substr(lp + 1, rp - lp - 1);
    if (inner.find("http") == 0) return inner;
    return "";
}

static void RunProcess(const std::string& cmdLine,
                       std::string& outStdout,
                       std::string& outStderr) {
    outStdout.clear(); outStderr.clear();
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hOutRd, hOutWr, hErrRd, hErrWr;
    if (!CreatePipe(&hOutRd, &hOutWr, &sa, 0) || !SetHandleInformation(hOutRd, HANDLE_FLAG_INHERIT, 0)) return;
    if (!CreatePipe(&hErrRd, &hErrWr, &sa, 0) || !SetHandleInformation(hErrRd, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hOutRd); CloseHandle(hOutWr); return;
    }
    STARTUPINFOA si = {};
    si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hOutWr; si.hStdError = hErrWr;
    PROCESS_INFORMATION pi = {};
    std::vector<char> cmd(cmdLine.begin(), cmdLine.end()); cmd.push_back('\0');
    if (!CreateProcessA(NULL, cmd.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hOutRd); CloseHandle(hOutWr); CloseHandle(hErrRd); CloseHandle(hErrWr); return;
    }
    CloseHandle(hOutWr); CloseHandle(hErrWr);
    char buf[4096]; DWORD dwRead, avail;
    DWORD deadline = GetTickCount() + 60000;
    while (true) {
        if (PeekNamedPipe(hOutRd,NULL,0,NULL,&avail,NULL) && avail > 0) {
            DWORD toRead = avail < sizeof(buf)-1 ? avail : sizeof(buf)-1;
            if (ReadFile(hOutRd, buf, toRead, &dwRead, NULL) && dwRead) { buf[dwRead]='\0'; outStdout+=buf; }
        }
        if (PeekNamedPipe(hErrRd,NULL,0,NULL,&avail,NULL) && avail > 0) {
            DWORD toRead = avail < sizeof(buf)-1 ? avail : sizeof(buf)-1;
            if (ReadFile(hErrRd, buf, toRead, &dwRead, NULL) && dwRead) { buf[dwRead]='\0'; outStderr+=buf; }
        }
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) break;
        if (GetTickCount() > deadline) { TerminateProcess(pi.hProcess, 1); break; }
    }
    while (PeekNamedPipe(hOutRd,NULL,0,NULL,&avail,NULL) && avail > 0) {
        DWORD toRead = avail < sizeof(buf)-1 ? avail : sizeof(buf)-1;
        if (ReadFile(hOutRd, buf, toRead, &dwRead, NULL) && dwRead) { buf[dwRead]='\0'; outStdout+=buf; }
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    CloseHandle(hOutRd); CloseHandle(hErrRd);
}

static void RemoveDirRecursive(const std::string& dir) {
    std::string pattern = dir + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            std::string full = dir + "\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveDirRecursive(full);
            else
                DeleteFileA(full.c_str());
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    RemoveDirectoryA(dir.c_str());
}

static std::string TryYtDlp(const std::string& videoId) {
    std::string ytdlp = FindYtDlp();
    if (ytdlp.empty()) return "";
    std::string videoUrl = "https://www.youtube.com/watch?v=" + videoId;
    std::string cmdLine =
        "\"" + ytdlp + "\""
        " --get-url"
        " -f \"best[ext=mp4]/bestvideo[ext=mp4]+bestaudio[ext=m4a]/best\""
        " --no-playlist --no-warnings"
        " -- \"" + videoUrl + "\"";
    std::string stdoutData, stderrData;
    RunProcess(cmdLine, stdoutData, stderrData);
    std::vector<std::string> urls;
    size_t pos = 0;
    while (pos <= stdoutData.size()) {
        size_t end = stdoutData.find('\n', pos);
        if (end == std::string::npos) end = stdoutData.size();
        std::string line = stdoutData.substr(pos, end - pos);
        while (!line.empty() && (line.back()=='\r'||line.back()=='\n')) line.pop_back();
        if (!line.empty()) { std::string url = ExtractUrlFromLine(line); if (!url.empty()) urls.push_back(url); }
        if (end == stdoutData.size()) break;
        pos = end + 1;
    }
    if (urls.empty()) return "";
    if (urls.size() == 1) return urls[0];
    return urls[0] + "\n" + urls[1];
}

// ---------------------------------------------------------------------------
// TryYtDlpQualities
// ---------------------------------------------------------------------------
static std::vector<VP9Quality> TryYtDlpQualities(const std::string& videoId) {
    std::vector<VP9Quality> result;

    std::string ytdlp = FindYtDlp();
    if (ytdlp.empty()) return result;

    std::string videoUrl = "https://www.youtube.com/watch?v=" + videoId;
    std::string cmdLine =
        "\"" + ytdlp + "\""
        " --dump-json"
        " --no-playlist --no-warnings"
        " -- \"" + videoUrl + "\"";

    std::string stdoutData, stderrData;
    RunProcess(cmdLine, stdoutData, stderrData);
    if (stdoutData.empty()) return result;

    try {
        auto j = json::parse(stdoutData);
        if (!j.contains("formats") || !j["formats"].is_array())
            return result;

        std::string bestAudioUrl;
        int bestAudioBr = 0;
        for (const auto& f : j["formats"]) {
            std::string vcodec = f.value("vcodec", "");
            std::string acodec = f.value("acodec", "");
            if (vcodec != "none" && !vcodec.empty()) continue;
            if (acodec.empty() || acodec == "none")  continue;

            bool isOpus = acodec.find("opus") != std::string::npos;
            bool isM4a  = acodec.find("mp4a") != std::string::npos;
            if (!isOpus && !isM4a) continue;

            std::string u = f.value("url", "");
            if (u.empty()) continue;

            int br = f.value("abr", 0);
            int effective = isOpus ? br + 10000 : br;
            if (effective > bestAudioBr) {
                bestAudioUrl = u;
                bestAudioBr  = effective;
            }
        }

        struct Bucket { VP9Quality q; int bitrate; };
        std::map<int, Bucket> byKey;

        for (const auto& f : j["formats"]) {
            std::string vcodec = f.value("vcodec", "");
            std::string acodec = f.value("acodec", "");

            if (acodec != "none" && !acodec.empty()) continue;
            if (vcodec.empty() || vcodec == "none")  continue;

            bool isVP9 = vcodec.find("vp9")  != std::string::npos ||
                         vcodec.find("vp09") != std::string::npos;
            bool isAV1 = vcodec.find("av01") != std::string::npos ||
                         vcodec.find("av1")  != std::string::npos;
            if (isAV1 || !isVP9) continue;

            int height = f.value("height", 0);
            int fps    = f.value("fps",    30);
            if (height <= 0) continue;

            std::string u = f.value("url", "");
            if (u.empty()) continue;

            char lbl[32];
            if (fps > 30)
                snprintf(lbl, sizeof(lbl), "%dp%d", height, fps);
            else
                snprintf(lbl, sizeof(lbl), "%dp", height);

            int bitrate = f.value("vbr", f.value("tbr", 0));
            int key     = height * 1000 + fps;

            auto it = byKey.find(key);
            if (it == byKey.end() || bitrate > it->second.bitrate) {
                VP9Quality q;
                q.label    = lbl;
                q.videoUrl = u;
                q.audioUrl = bestAudioUrl;
                q.height   = height;
                byKey[key] = { q, bitrate };
            }
        }

        std::vector<std::pair<int, Bucket>> sorted(byKey.begin(), byKey.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b){ return a.first > b.first; });
        for (auto& p : sorted)
            result.push_back(p.second.q);

    } catch (...) {
        DIAG("[YtDlpQualities] JSON parse failed for %s", videoId.c_str());
    }

    DIAG("[YtDlpQualities] found %d VP9 entries for %s",
         (int)result.size(), videoId.c_str());
    return result;
}

static std::string BestUrl(const json& sd) {
    auto extractUrl = [&](const json& fmt) -> std::string {
        if (fmt.contains("url")) { std::string u = fmt.value("url", ""); if (!u.empty()) return u; }
        for (const char* k : { "signatureCipher", "cipher" }) {
            if (fmt.contains(k)) {
                std::string sc = fmt.value(k, "");
                if (!sc.empty()) { std::string u = SigDecipher::DecodeCipher(sc); if (!u.empty()) return u; }
            }
        }
        return "";
    };
    if (sd.contains("formats")) {
        for (const auto& f : sd["formats"])
            if (f.value("itag", 0) == 18) { auto u = extractUrl(f); if (!u.empty()) return u; }
        for (const auto& f : sd["formats"]) { auto u = extractUrl(f); if (!u.empty()) return u; }
    }
    if (sd.contains("adaptiveFormats"))
        for (const auto& f : sd["adaptiveFormats"]) {
            if (!f.contains("audioQuality")) continue;
            auto u = extractUrl(f); if (!u.empty()) return u;
        }
    return "";
}

static std::string TryDirectClient(const std::string& ctxBase,
                                    const std::string& videoId,
                                    const char* clientName,
                                    const char* clientVersion) {
    json body = json::parse(ctxBase);
    body["videoId"] = videoId;
    body["contentCheckOk"] = true; body["racyCheckOk"] = true;
    std::string resp = InnerTube::Post("/youtubei/v1/player?prettyPrint=false",
                                       body.dump(), clientName, clientVersion);
    if (resp.empty()) return "";
    try {
        auto j = json::parse(resp);
        if (j.contains("playabilityStatus")) {
            std::string st = j["playabilityStatus"].value("status", "");
            if (st == "ERROR" || st == "UNPLAYABLE") return "";
        }
        if (!j.contains("streamingData")) return "";
        for (const char* arr : { "formats", "adaptiveFormats" }) {
            if (!j["streamingData"].contains(arr)) continue;
            for (const auto& f : j["streamingData"][arr])
                if (f.contains("url") || f.contains("signatureCipher") || f.contains("cipher"))
                    return BestUrl(j["streamingData"]);
        }
    } catch (...) {}
    return "";
}

static std::string TryCipherClient(const std::string& videoId) {
    if (!SigDecipher::EnsureLoaded()) return "";
    std::string sigTs = SigDecipher::GetSigTimestamp();
    json body = json::parse(kCtxTV);
    body["videoId"] = videoId;
    body["contentCheckOk"] = true; body["racyCheckOk"] = true;
    if (!sigTs.empty())
        body["playbackContext"] = {{"contentPlaybackContext", {{"signatureTimestamp", sigTs}}}};
    std::string resp = InnerTube::Post("/youtubei/v1/player?prettyPrint=false",
                                       body.dump(), "TVHTML5_SIMPLY_EMBEDDED_PLAYER", "2.0");
    if (resp.empty()) return "";
    try {
        auto j = json::parse(resp);
        if (j.contains("playabilityStatus")) {
            std::string st = j["playabilityStatus"].value("status", "");
            if (st == "ERROR" || st == "UNPLAYABLE") return "";
        }
        if (j.contains("streamingData")) return BestUrl(j["streamingData"]);
    } catch (...) {}
    return "";
}

// ---------------------------------------------------------------------------
// GetVideoInfo
// ---------------------------------------------------------------------------
VideoInfo InnerTube::GetVideoInfo(const std::string& videoId) {
    VideoInfo info;
    json body = json::parse(kCtxWeb);
    body["videoId"] = videoId;
    body["contentCheckOk"] = true; body["racyCheckOk"] = true;
    std::string resp = Post("/youtubei/v1/player?prettyPrint=false", body.dump(),
                            "WEB", "2.20240101.00.00");
    if (resp.empty()) return info;
    try {
        auto j = json::parse(resp);
        if (j.contains("videoDetails")) {
            const auto& vd = j["videoDetails"];
            info.description = vd.value("shortDescription", "");
            info.channelId   = vd.value("channelId",        "");
            info.viewCount   = vd.value("viewCount",        "");
        }
    } catch (...) {}
    return info;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------
std::vector<VideoItem> InnerTube::Search(const std::string& query, std::string* errorOut) {
    std::vector<VideoItem> items;
    if (errorOut) *errorOut = "";
    json body = json::parse(kCtxWeb);
    body["query"] = query;
    body["contentCheckOk"] = true; body["racyCheckOk"] = true;
    std::string resp = Post("/youtubei/v1/search?prettyPrint=false", body.dump(),
                            "WEB", "2.20240101.00.00");
    if (resp.empty()) {
        if (errorOut) *errorOut = "Network error";
        return items;
    }
    try {
        auto j = json::parse(resp);
        auto& primary = j["contents"]["twoColumnSearchResultsRenderer"]["primaryContents"];
        auto& sections = primary["sectionListRenderer"]["contents"];
        for (auto& sec : sections) {
            if (!sec.contains("itemSectionRenderer")) continue;
            for (auto& item : sec["itemSectionRenderer"]["contents"]) {
                if (!item.contains("videoRenderer")) continue;
                const auto& vr = item["videoRenderer"];
                VideoItem vi;
                vi.videoId = vr.value("videoId", "");
                if (vr.contains("title"))
                    vi.title = ToW(TextNode(vr["title"]));
                if (vr.contains("longBylineText"))
                    vi.channel = ToW(TextNode(vr["longBylineText"]));
                else if (vr.contains("shortBylineText"))
                    vi.channel = ToW(TextNode(vr["shortBylineText"]));
                if (vr.contains("lengthText"))
                    vi.duration = ToW(TextNode(vr["lengthText"]));
                if (vr.contains("shortViewCountText"))
                    vi.views = ToW(TextNode(vr["shortViewCountText"]));
                if (vr.contains("thumbnail") && vr["thumbnail"].contains("thumbnails")) {
                    const auto& thumbs = vr["thumbnail"]["thumbnails"];
                    if (!thumbs.empty())
                        vi.thumbnailUrl = thumbs.back().value("url", "");
                }
                if (!vi.videoId.empty())
                    items.push_back(vi);
            }
        }
    } catch (const std::exception& e) {
        if (errorOut) *errorOut = std::string("Parse error: ") + e.what();
    }
    return items;
}

// ---------------------------------------------------------------------------
// GetStreamUrl
// ---------------------------------------------------------------------------
std::string InnerTube::GetStreamUrl(const std::string& videoId) {
    std::string url;

    url = TryDirectClient(kCtxWeb,      videoId, "WEB",                          "2.20240101.00.00");
    if (!url.empty()) return url;

    url = TryDirectClient(kCtxWebEmbed, videoId, "WEB_EMBEDDED_PLAYER",          "2.20231219.01.00");
    if (!url.empty()) return url;

    url = TryCipherClient(videoId);
    if (!url.empty()) return url;

    url = TryYtDlp(videoId);
    return url;
}

// ---------------------------------------------------------------------------
// GetVP9Qualities
// ---------------------------------------------------------------------------
std::vector<VP9Quality> InnerTube::GetVP9Qualities(const std::string& videoId) {
    std::vector<VP9Quality> result;

    json body = json::parse(kCtxWeb);
    body["videoId"] = videoId;
    body["contentCheckOk"] = true;
    body["racyCheckOk"]    = true;
    std::string resp = Post("/youtubei/v1/player?prettyPrint=false", body.dump(),
                            "WEB", "2.20240101.00.00");

    if (!resp.empty()) {
        try {
            auto j = json::parse(resp);
            if (j.contains("streamingData") && j["streamingData"].contains("adaptiveFormats")) {
                std::string bestAudioUrl;
                int bestAudioBr = 0;

                for (const auto& f : j["streamingData"]["adaptiveFormats"]) {
                    std::string mime  = f.value("mimeType", "");
                    if (mime.find("video") != std::string::npos) continue;
                    if (mime.find("audio") == std::string::npos) continue;

                    bool isOpus = mime.find("opus") != std::string::npos;
                    bool isM4a  = mime.find("mp4a") != std::string::npos ||
                                  mime.find("m4a")  != std::string::npos;
                    if (!isOpus && !isM4a) continue;

                    std::string u = f.value("url", "");
                    if (u.empty()) continue;

                    int br = f.value("averageBitrate", f.value("bitrate", 0));
                    int effective = isOpus ? br + 1000000 : br;
                    if (effective > bestAudioBr) { bestAudioUrl = u; bestAudioBr = effective; }
                }

                struct Bucket { VP9Quality q; int bitrate; };
                std::map<int, Bucket> byKey;

                for (const auto& f : j["streamingData"]["adaptiveFormats"]) {
                    std::string mime = f.value("mimeType", "");
                    if (mime.find("video") == std::string::npos) continue;

                    bool isVP9 = mime.find("vp9")  != std::string::npos ||
                                 mime.find("vp09") != std::string::npos;
                    bool isAV1 = mime.find("av01") != std::string::npos;
                    if (isAV1 || !isVP9) continue;

                    int height = f.value("height", 0);
                    int fps    = f.value("fps",    30);
                    if (height <= 0) continue;

                    std::string u = f.value("url", "");
                    if (u.empty()) continue;

                    char lbl[32];
                    if (fps > 30) snprintf(lbl, sizeof(lbl), "%dp%d", height, fps);
                    else          snprintf(lbl, sizeof(lbl), "%dp",   height);

                    int bitrate = f.value("averageBitrate", f.value("bitrate", 0));
                    int key     = height * 1000 + fps;

                    auto it = byKey.find(key);
                    if (it == byKey.end() || bitrate > it->second.bitrate) {
                        VP9Quality q;
                        q.label    = lbl;
                        q.videoUrl = u;
                        q.audioUrl = bestAudioUrl;
                        q.height   = height;
                        byKey[key] = { q, bitrate };
                    }
                }

                std::vector<std::pair<int,Bucket>> sorted(byKey.begin(), byKey.end());
                std::sort(sorted.begin(), sorted.end(),
                          [](const auto& a, const auto& b){ return a.first > b.first; });
                for (auto& p : sorted)
                    result.push_back(p.second.q);
            }
        } catch (...) {
            DIAG("[GetVP9Qualities] JSON parse error for %s", videoId.c_str());
        }
    }

    if (!result.empty()) {
        DIAG("[GetVP9Qualities] InnerTube returned %d qualities for %s",
             (int)result.size(), videoId.c_str());
        return result;
    }

    DIAG("[GetVP9Qualities] InnerTube empty, falling back to yt-dlp for %s", videoId.c_str());
    result = TryYtDlpQualities(videoId);

    if (result.empty()) {
        VP9Quality fallback;
        fallback.label    = "Auto";
        fallback.videoUrl = GetStreamUrl(videoId);
        fallback.audioUrl = "";
        fallback.height   = 0;
        result.push_back(fallback);
        DIAG("[GetVP9Qualities] using Auto fallback for %s", videoId.c_str());
    }
    return result;
}

// ---------------------------------------------------------------------------
// GetComments  (InnerTube /next  two-step approach)
// ---------------------------------------------------------------------------

// FIX 2: CollapseRuns con fallback a plain string y DIAG agresivo
static std::string CollapseRuns(const json& node) {
    if (node.is_null()) return "";
    if (node.is_string()) return node.get<std::string>();
    if (node.contains("simpleText") && node["simpleText"].is_string())
        return node["simpleText"].get<std::string>();
    if (node.contains("runs") && node["runs"].is_array()) {
        std::string out;
        for (const auto& r : node["runs"]) {
            if (r.is_object() && r.contains("text") && r["text"].is_string())
                out += r["text"].get<std::string>();
        }
        return out;
    }
    // FIX 2a: leer "content" como string plano antes de recursar
    if (node.contains("content") && node["content"].is_string())
        return node["content"].get<std::string>();
    if (node.contains("content") && node["content"].is_object())
        return CollapseRuns(node["content"]);
    // FIX 2b: DIAG agresivo — loguea primeros 300 chars si nada funcionó
    {
        std::string raw = node.dump();
        if (raw.size() > 300) raw = raw.substr(0, 300) + "...";
        DIAG("[CollapseRuns] no known structure, raw=%.300s", raw.c_str());
    }
    return "";
}

static CommentsPage ExtractComments(const json& j) {
    CommentsPage page;

    const json* mutations = nullptr;
    if (j.contains("frameworkUpdates") &&
        j["frameworkUpdates"].contains("entityBatchUpdate") &&
        j["frameworkUpdates"]["entityBatchUpdate"].contains("mutations")) {
        mutations = &j["frameworkUpdates"]["entityBatchUpdate"]["mutations"];
    }

    if (mutations && mutations->is_array()) {
        DIAG("[GetComments] EUVM path: %d mutations", (int)mutations->size());
        for (const auto& m : *mutations) {
            if (!m.contains("payload")) continue;
            const auto& payload = m["payload"];
            if (!payload.contains("commentEntityPayload")) continue;
            const auto& cep = payload["commentEntityPayload"];
            CommentItem ci;
            if (cep.contains("author") && cep["author"].contains("displayName"))
                ci.authorName = CollapseRuns(cep["author"]["displayName"]);
            if (cep.contains("properties")) {
                const auto& props = cep["properties"];
                if (props.contains("content")) {
                    const auto& content = props["content"];
                    DIAG("[GetComments] mutation content node type=%s empty=%d",
                         content.type_name(), CollapseRuns(content).empty() ? 1 : 0);
                    ci.text = CollapseRuns(content);
                }
                if (props.contains("publishedTime") && props["publishedTime"].is_string())
                    ci.publishedAt = props["publishedTime"].get<std::string>();
            }
            if (cep.contains("toolbar")) {
                const auto& tb = cep["toolbar"];
                if (tb.contains("likeCountNotliked") && tb["likeCountNotliked"].is_string())
                    ci.likeCount = tb["likeCountNotliked"].get<std::string>();
            }
            if (!ci.text.empty() || !ci.authorName.empty())
                page.comments.push_back(ci);
        }
    }

    if (page.comments.empty()) {
        DIAG("[GetComments] Trying legacy renderer path");
        auto tryLegacy = [&](const json& arr) {
            if (!arr.is_array()) return;
            for (const auto& item : arr) {
                const json* cr = nullptr;
                if (item.contains("commentRenderer"))
                    cr = &item["commentRenderer"];
                else if (item.contains("commentThreadRenderer") &&
                         item["commentThreadRenderer"].contains("comment") &&
                         item["commentThreadRenderer"]["comment"].contains("commentRenderer"))
                    cr = &item["commentThreadRenderer"]["comment"]["commentRenderer"];
                if (!cr) continue;
                CommentItem ci;
                if ((*cr).contains("authorText"))  ci.authorName = TextNode((*cr)["authorText"]);
                if ((*cr).contains("contentText")) ci.text       = TextNode((*cr)["contentText"]);
                if ((*cr).contains("voteCount"))   ci.likeCount  = TextNode((*cr)["voteCount"]);
                if ((*cr).contains("publishedTimeText"))
                    ci.publishedAt = TextNode((*cr)["publishedTimeText"]);
                if (!ci.text.empty()) page.comments.push_back(ci);
            }
        };
        if (j.contains("continuationContents") &&
            j["continuationContents"].contains("itemSectionContinuation") &&
            j["continuationContents"]["itemSectionContinuation"].contains("contents"))
            tryLegacy(j["continuationContents"]["itemSectionContinuation"]["contents"]);
        if (j.contains("onResponseReceivedEndpoints"))
            for (const auto& ep : j["onResponseReceivedEndpoints"]) {
                if (ep.contains("reloadContinuationItemsCommand") &&
                    ep["reloadContinuationItemsCommand"].contains("continuationItems"))
                    tryLegacy(ep["reloadContinuationItemsCommand"]["continuationItems"]);
                if (ep.contains("appendContinuationItemsAction") &&
                    ep["appendContinuationItemsAction"].contains("continuationItems"))
                    tryLegacy(ep["appendContinuationItemsAction"]["continuationItems"]);
            }
    }

    auto findToken = [&](const json& arr) {
        if (!arr.is_array()) return;
        for (const auto& item : arr) {
            if (item.contains("continuationItemRenderer")) {
                const auto& cir = item["continuationItemRenderer"];
                if (cir.contains("continuationEndpoint") &&
                    cir["continuationEndpoint"].contains("continuationCommand") &&
                    cir["continuationEndpoint"]["continuationCommand"].contains("token")) {
                    page.continuationToken =
                        cir["continuationEndpoint"]["continuationCommand"]["token"].get<std::string>();
                    return;
                }
            }
        }
    };
    if (j.contains("onResponseReceivedEndpoints"))
        for (const auto& ep : j["onResponseReceivedEndpoints"]) {
            if (ep.contains("reloadContinuationItemsCommand"))
                findToken(ep["reloadContinuationItemsCommand"]["continuationItems"]);
            if (ep.contains("appendContinuationItemsAction"))
                findToken(ep["appendContinuationItemsAction"]["continuationItems"]);
        }

    DIAG("[GetComments] extracted %d comments, next_token=%s",
         (int)page.comments.size(),
         page.continuationToken.empty() ? "(none)" : "yes");
    return page;
}

// FIX 1: helper para volcar resp2 a disco (debug temporal)
static void DumpResp2(const std::string& data) {
    HANDLE hF = CreateFileA(
        "C:\\ct_debug_resp2.json",
        GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hF == INVALID_HANDLE_VALUE) return;
    DWORD dw;
    WriteFile(hF, data.c_str(), (DWORD)data.size(), &dw, NULL);
    CloseHandle(hF);
    DIAG("[GetComments] resp2 dumped to C:\\ct_debug_resp2.json (%lu bytes)", (DWORD)data.size());
}

CommentsPage InnerTube::GetComments(const std::string& videoId,
                                     const std::string& continuationToken) {
    CommentsPage page;

    if (continuationToken.empty()) {
        json body1 = json::parse(kCtxWeb);
        body1["videoId"]        = videoId;
        body1["contentCheckOk"] = true;
        body1["racyCheckOk"]    = true;

        std::string resp1 = Post("/youtubei/v1/next?prettyPrint=false", body1.dump(),
                                 "WEB", "2.20240101.00.00");
        if (resp1.empty()) { page.error = "Step1 network error"; return page; }

        std::string tok1;
        try {
            auto j1 = json::parse(resp1);

            if (j1.contains("engagementPanels")) {
                for (const auto& panel : j1["engagementPanels"]) {
                    const auto& epslr = panel.value("engagementPanelSectionListRenderer", json{});
                    if (epslr.value("panelIdentifier", "") != "engagement-panel-comments-section") continue;
                    const auto& content = epslr.value("content", json{});
                    const auto& slr     = content.value("sectionListRenderer", json{});
                    if (!slr.contains("contents")) continue;
                    for (const auto& c : slr["contents"]) {
                        const auto& isr = c.value("itemSectionRenderer", json{});
                        if (!isr.contains("contents")) continue;
                        for (const auto& ic : isr["contents"]) {
                            if (ic.contains("continuationItemRenderer")) {
                                const auto& cir = ic["continuationItemRenderer"];
                                if (cir.contains("continuationEndpoint") &&
                                    cir["continuationEndpoint"].contains("continuationCommand")) {
                                    tok1 = cir["continuationEndpoint"]["continuationCommand"].value("token", "");
                                }
                            }
                        }
                    }
                    if (!tok1.empty()) break;
                }
            }

            if (tok1.empty() && j1.contains("onResponseReceivedEndpoints")) {
                for (const auto& ep : j1["onResponseReceivedEndpoints"]) {
                    for (const char* key : {"reloadContinuationItemsCommand","appendContinuationItemsAction"}) {
                        if (!ep.contains(key)) continue;
                        const auto& items = ep[key].value("continuationItems", json::array());
                        for (const auto& item : items) {
                            if (!item.contains("continuationItemRenderer")) continue;
                            const auto& cir = item["continuationItemRenderer"];
                            if (cir.contains("continuationEndpoint") &&
                                cir["continuationEndpoint"].contains("continuationCommand") &&
                                cir["continuationEndpoint"]["continuationCommand"].contains("token")) {
                                tok1 = cir["continuationEndpoint"]["continuationCommand"]["token"].get<std::string>();
                                break;
                            }
                        }
                        if (!tok1.empty()) break;
                    }
                    if (!tok1.empty()) break;
                }
            }

            if (tok1.empty() &&
                j1.contains("contents") &&
                j1["contents"].contains("twoColumnWatchNextResults")) {
                try {
                    const auto& tcwnr   = j1["contents"]["twoColumnWatchNextResults"];
                    const auto& results = tcwnr["results"]["results"]["contents"];
                    if (results.is_array()) {
                        for (const auto& sec : results) {
                            if (!sec.contains("itemSectionRenderer")) continue;
                            const auto& isr = sec["itemSectionRenderer"];
                            bool isComments = isr.value("targetId", "") == "comments-section";
                            if (!isr.contains("contents")) continue;
                            for (const auto& ic : isr["contents"]) {
                                if (!ic.contains("continuationItemRenderer")) continue;
                                const auto& cir = ic["continuationItemRenderer"];
                                if (!cir.contains("continuationEndpoint")) continue;
                                const auto& ce = cir["continuationEndpoint"];
                                if (!ce.contains("continuationCommand")) continue;
                                const auto& cc = ce["continuationCommand"];
                                if (!cc.contains("token")) continue;
                                std::string candidate = cc["token"].get<std::string>();
                                if (!candidate.empty()) {
                                    tok1 = candidate;
                                    DIAG("[GetComments] Path C token found (isComments=%d)", isComments?1:0);
                                    break;
                                }
                            }
                            if (!tok1.empty()) break;
                        }
                    }
                } catch (...) {}
            }

        } catch (const std::exception& e) {
            page.error = std::string("Step1 parse error: ") + e.what();
            return page;
        }

        DIAG("[GetComments] Step1 token=%s", tok1.empty() ? "(none)" : tok1.substr(0,40).c_str());

        if (tok1.empty()) {
            page.error = "Could not find comments continuation token";
            return page;
        }

        json body2 = json::parse(kCtxWeb);
        body2["continuation"] = tok1;
        std::string resp2 = Post("/youtubei/v1/next?prettyPrint=false", body2.dump(),
                                 "WEB", "2.20240101.00.00");
        if (resp2.empty()) { page.error = "Step2 network error"; return page; }

        // FIX 1: volcar resp2 a disco para inspección
        DumpResp2(resp2);

        try {
            auto j2 = json::parse(resp2);
            page = ExtractComments(j2);
        } catch (const std::exception& e) {
            page.error = std::string("Step2 parse error: ") + e.what();
        }

    } else {
        json body = json::parse(kCtxWeb);
        body["continuation"] = continuationToken;
        std::string resp = Post("/youtubei/v1/next?prettyPrint=false", body.dump(),
                                "WEB", "2.20240101.00.00");
        if (resp.empty()) { page.error = "Continuation network error"; return page; }
        try {
            auto j = json::parse(resp);
            page = ExtractComments(j);
        } catch (const std::exception& e) {
            page.error = std::string("Continuation parse error: ") + e.what();
        }
    }

    return page;
}

// ---------------------------------------------------------------------------
// GetRelatedVideos  (/youtubei/v1/next -> twoColumnWatchNextResults)
// ---------------------------------------------------------------------------

static std::string BestThumb(const json& node) {
    if (!node.contains("thumbnails") || !node["thumbnails"].is_array()) return "";
    const auto& arr = node["thumbnails"];
    std::string best;
    for (const auto& t : arr) {
        std::string u = t.value("url", "");
        if (!u.empty()) best = u;
        if (t.value("width", 0) >= 120) break;
    }
    return best;
}

// ---------------------------------------------------------------------------
// ParseLockupViewModel
//
// Aligned with NewPipe YoutubeStreamInfoItemLockupExtractor (2025-07).
//
// KEY FIX: YouTube sends "contentType", NOT "lockupType".
//
// Field paths (verified against NewPipe source):
//   contentType:   lockupViewModel["contentType"]
//   contentId:     lockupViewModel["contentId"]
//   title:         lockupViewModel["metadata"]["lockupMetadataViewModel"]["title"]["content"]
//   metadataRows:  lockupViewModel["metadata"]["lockupMetadataViewModel"]["metadata"]
//                    ["contentMetadataViewModel"]["metadataRows"]
//     each row -> metadataParts[] -> text["content"]
//     row 0, part 0 = channel name
//     row 1, part 0 = views  (search "view" keyword as fallback)
//     row 1, part 1 = date
//   thumbnail:     lockupViewModel["contentImage"]["thumbnailViewModel"]["image"]["sources"]
//                    prefer largest "width"
//   duration:      lockupViewModel["contentImage"]["thumbnailViewModel"]["overlays"][]
//                    ["thumbnailBottomOverlayViewModel"]["badges"][]
//                    ["thumbnailBadgeViewModel"]["text"]  (pick entry that contains a digit)
// ---------------------------------------------------------------------------
static bool ParseLockupViewModel(const json& lvm, RelatedItem& ri) {
    // --- contentType (NewPipe: lockupViewModel.getString("contentType")) ---
    std::string contentType = lvm.value("contentType", "");

    bool isVideo    = (contentType == "LOCKUP_CONTENT_TYPE_VIDEO");
    bool isPlaylist = (contentType == "LOCKUP_CONTENT_TYPE_PLAYLIST" ||
                       contentType == "LOCKUP_CONTENT_TYPE_PODCAST"  ||
                       contentType == "LOCKUP_CONTENT_TYPE_RADIO");

    if (!isVideo && !isPlaylist) {
        DIAG("[ParseLockup] unknown contentType: '%s'", contentType.c_str());
        return false;
    }

    // --- contentId ---
    std::string contentId = lvm.value("contentId", "");
    if (contentId.empty()) {
        // Fallback: watchEndpoint inside rendererContext
        try {
            contentId = lvm.at("rendererContext")
                           .at("commandContext")
                           .at("onTap")
                           .at("innertubeCommand")
                           .at("watchEndpoint")
                           .value("videoId", "");
        } catch (...) {}
    }
    if (contentId.empty()) return false;

    if (isVideo)    ri.videoId    = contentId;
    else            ri.playlistId = contentId;
    ri.isPlaylist = isPlaylist;

    // --- Title ---
    // NewPipe: JsonUtils.getString(lockupViewModel,
    //   "metadata.lockupMetadataViewModel.title.content")
    try {
        ri.title = lvm.at("metadata")
                      .at("lockupMetadataViewModel")
                      .at("title")
                      .value("content", "");
    } catch (...) {}

    // --- Metadata rows (channel, views, date) ---
    // NewPipe: JsonUtils.getArray(lockupViewModel,
    //   "metadata.lockupMetadataViewModel.metadata.contentMetadataViewModel.metadataRows")
    try {
        const auto& rows = lvm.at("metadata")
                              .at("lockupMetadataViewModel")
                              .at("metadata")
                              .at("contentMetadataViewModel")
                              .at("metadataRows");

        if (rows.is_array()) {
            // Row 0 -> channel name (part 0, text.content)
            if (rows.size() >= 1 && rows[0].contains("metadataParts")) {
                const auto& parts0 = rows[0]["metadataParts"];
                if (parts0.is_array() && !parts0.empty()) {
                    try {
                        ri.channelName = parts0[0].at("text").value("content", "");
                    } catch (...) {}
                }
            }

            // Row 1 -> views + date (search by keyword for robustness)
            // NewPipe searches for text.contains("views") in any part of the info row
            int infoRow = (rows.size() >= 2) ? 1 : 0;
            if ((int)rows.size() > infoRow && rows[infoRow].contains("metadataParts")) {
                const auto& parts1 = rows[infoRow]["metadataParts"];
                if (parts1.is_array()) {
                    for (const auto& part : parts1) {
                        try {
                            std::string val = part.at("text").value("content", "");
                            if (val.empty()) continue;
                            // Views: contains digit + "view"
                            std::string lower = val;
                            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                            if (ri.viewCount.empty() &&
                                (lower.find("view") != std::string::npos ||
                                 lower.find("watch") != std::string::npos))
                                ri.viewCount = val;
                            // Date: ends with "ago" or starts with "Premiere"
                            else if (ri.publishedAt.empty() &&
                                     (lower.size() >= 3 &&
                                      lower.substr(lower.size()-3) == "ago"))
                                ri.publishedAt = val;
                            // First part of row 1 goes to views if still empty
                            else if (ri.viewCount.empty())
                                ri.viewCount = val;
                        } catch (...) {}
                    }
                }
            }
        }
    } catch (...) {}

    // --- Thumbnail ---
    // NewPipe: JsonUtils.getArray(lockupViewModel,
    //   "contentImage.thumbnailViewModel.image.sources")
    try {
        const auto& sources = lvm.at("contentImage")
                                 .at("thumbnailViewModel")
                                 .at("image")
                                 .at("sources");
        if (sources.is_array()) {
            std::string bestUrl;
            int bestW = 0;
            for (const auto& src : sources) {
                std::string u = src.value("url", "");
                int w = src.value("width", 0);
                if (!u.empty() && w > bestW) { bestUrl = u; bestW = w; }
            }
            if (!bestUrl.empty()) ri.thumbnailUrl = bestUrl;
        }
    } catch (...) {}

    // --- Duration ---
    // NewPipe: contentImage.thumbnailViewModel.overlays[]
    //   .thumbnailBottomOverlayViewModel.badges[].thumbnailBadgeViewModel.text
    //   (pick value that contains a digit)
    try {
        const auto& overlays = lvm.at("contentImage")
                                  .at("thumbnailViewModel")
                                  .at("overlays");
        if (overlays.is_array()) {
            for (const auto& ov : overlays) {
                try {
                    const auto& badges = ov.at("thumbnailBottomOverlayViewModel").at("badges");
                    if (!badges.is_array()) continue;
                    for (const auto& badge : badges) {
                        try {
                            std::string text = badge.at("thumbnailBadgeViewModel").value("text", "");
                            if (!text.empty() &&
                                text.find_first_of("0123456789") != std::string::npos) {
                                ri.duration = text;
                                break;
                            }
                        } catch (...) {}
                    }
                } catch (...) {}
                if (!ri.duration.empty()) break;
            }
        }
    } catch (...) {}

    DIAG("[ParseLockup] contentType=%s id=%s title=%.40s ch=%.30s dur=%s",
         contentType.c_str(), contentId.c_str(),
         ri.title.c_str(), ri.channelName.c_str(), ri.duration.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// ParseRelatedEntry
// ---------------------------------------------------------------------------
static bool ParseRelatedEntry(const json& entry, RelatedItem& ri) {
    const json* node = &entry;
    json unwrapped;
    if (entry.contains("richItemRenderer") &&
        entry["richItemRenderer"].contains("content")) {
        unwrapped = entry["richItemRenderer"]["content"];
        node = &unwrapped;
    }

    if (node->contains("compactVideoRenderer")) {
        const auto& cvr = (*node)["compactVideoRenderer"];
        ri.videoId     = cvr.value("videoId", "");
        ri.title       = TextNode(cvr.value("title",              json{}));
        ri.channelName = TextNode(cvr.value("longBylineText",     json{}));
        if (ri.channelName.empty())
            ri.channelName = TextNode(cvr.value("shortBylineText", json{}));
        ri.viewCount   = TextNode(cvr.value("shortViewCountText", json{}));
        ri.duration    = TextNode(cvr.value("lengthText",         json{}));
        if (cvr.contains("thumbnail"))
            ri.thumbnailUrl = BestThumb(cvr["thumbnail"]);
        ri.isPlaylist  = false;
        return true;
    }
    if (node->contains("compactPlaylistRenderer")) {
        const auto& cpr = (*node)["compactPlaylistRenderer"];
        ri.playlistId  = cpr.value("playlistId", "");
        ri.title       = TextNode(cpr.value("title",          json{}));
        ri.channelName = TextNode(cpr.value("longBylineText", json{}));
        if (ri.channelName.empty())
            ri.channelName = TextNode(cpr.value("shortBylineText", json{}));
        if (cpr.contains("thumbnail"))
            ri.thumbnailUrl = BestThumb(cpr["thumbnail"]);
        ri.isPlaylist  = true;
        return true;
    }
    if (node->contains("compactRadioRenderer")) {
        const auto& crr = (*node)["compactRadioRenderer"];
        ri.videoId     = crr.value("videoId", "");
        ri.title       = TextNode(crr.value("title",          json{}));
        ri.channelName = TextNode(crr.value("longBylineText", json{}));
        if (ri.channelName.empty())
            ri.channelName = TextNode(crr.value("shortBylineText", json{}));
        if (crr.contains("thumbnail"))
            ri.thumbnailUrl = BestThumb(crr["thumbnail"]);
        ri.isPlaylist  = true;
        return true;
    }

    if (node->contains("lockupViewModel")) {
        return ParseLockupViewModel((*node)["lockupViewModel"], ri);
    }

    return false;
}

// ---------------------------------------------------------------------------
// GetRelatedVideos
// ---------------------------------------------------------------------------
std::vector<RelatedItem> InnerTube::GetRelatedVideos(const std::string& videoId) {
    std::vector<RelatedItem> items;

    json body = json::parse(kCtxWeb);
    body["videoId"]        = videoId;
    body["contentCheckOk"] = true;
    body["racyCheckOk"]    = true;

    std::string resp = Post("/youtubei/v1/next?prettyPrint=false", body.dump(),
                            "WEB", "2.20240101.00.00");
    if (resp.empty()) {
        DIAG("[GetRelatedVideos] empty response for %s", videoId.c_str());
        return items;
    }

    try {
        auto j = json::parse(resp);

        if (!j.contains("contents")) {
            DIAG("[GetRelatedVideos] no 'contents' key");
            return items;
        }
        const auto& contents = j["contents"];
        if (!contents.contains("twoColumnWatchNextResults")) {
            DIAG("[GetRelatedVideos] no 'twoColumnWatchNextResults' key");
            return items;
        }
        const auto& tcwnr = contents["twoColumnWatchNextResults"];
        if (!tcwnr.contains("secondaryResults")) {
            DIAG("[GetRelatedVideos] no 'secondaryResults' (outer)");
            return items;
        }
        const auto& sr1 = tcwnr["secondaryResults"];
        if (!sr1.contains("secondaryResults")) {
            DIAG("[GetRelatedVideos] no 'secondaryResults' (inner)");
            return items;
        }
        const auto& sr2 = sr1["secondaryResults"];

        const json* resultsArr = nullptr;
        if (sr2.contains("results") && sr2["results"].is_array())
            resultsArr = &sr2["results"];
        else if (sr2.contains("items") && sr2["items"].is_array())
            resultsArr = &sr2["items"];

        if (!resultsArr) {
            std::string keys;
            for (auto it = sr2.begin(); it != sr2.end(); ++it)
                keys += it.key() + std::string(",");
            DIAG("[GetRelatedVideos] sr2 keys: %s", keys.c_str());
            return items;
        }

        DIAG("[GetRelatedVideos] results array size=%d", (int)resultsArr->size());

        for (const auto& entry : *resultsArr) {
            RelatedItem ri;
            if (!ParseRelatedEntry(entry, ri)) continue;
            if (ri.videoId.empty() && ri.playlistId.empty()) continue;
            items.push_back(ri);
        }

    } catch (...) {
        DIAG("[GetRelatedVideos] parse exception for %s", videoId.c_str());
    }

    DIAG("[GetRelatedVideos] found %d items for %s", (int)items.size(), videoId.c_str());
    return items;
}
