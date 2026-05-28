#pragma once
// SigDecipher.h  --  YouTube signature decipher (no JS engine required)
//
// Algorithm:
//  1. GET /iframe_api  -> extract player version
//  2. GET /s/player/{ver}/player_ias.vflset/en_US/base.js
//       -> find decipher function name, extract body, extract helper ops
//  3. POST /youtubei/v1/player with TVHTML5 client + signatureTimestamp
//  4. Decode signatureCipher.s with extracted ops
//  5. Append &sig=<decoded> to URL

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <algorithm>
#include <stdio.h>

#pragma comment(lib, "winhttp.lib")

#define SD_LOG(fmt, ...) do { \
    char _sb[512]; \
    snprintf(_sb, sizeof(_sb), "[CT] [SigDecipher] " fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(_sb); \
} while(0)

namespace SigDecipher {

// ---------------------------------------------------------------------------
// WinHTTP GET
// ---------------------------------------------------------------------------
static std::string HttpGet(const wchar_t* host, const wchar_t* path) {
    std::string result;
    HINTERNET hSess = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) { SD_LOG("WinHttpOpen failed: %lu", GetLastError()); return result; }
    DWORD secProto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSess, WINHTTP_OPTION_SECURE_PROTOCOLS, &secProto, sizeof(secProto));
    WinHttpSetTimeouts(hSess, 15000, 15000, 30000, 30000);
    HINTERNET hConn = WinHttpConnect(hSess, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { SD_LOG("WinHttpConnect failed: %lu", GetLastError()); WinHttpCloseHandle(hSess); return result; }
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hReq) { SD_LOG("WinHttpOpenRequest failed: %lu", GetLastError()); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result; }
    WinHttpAddRequestHeaders(hReq, L"Accept-Language: en-US,en;q=0.9\r\nAccept: */*", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) {
        SD_LOG("WinHttpSendRequest failed: %lu", GetLastError());
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result;
    }
    if (!WinHttpReceiveResponse(hReq, NULL)) {
        SD_LOG("WinHttpReceiveResponse failed: %lu", GetLastError());
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result;
    }
    DWORD status = 0, sz = sizeof(DWORD);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);
    SD_LOG("GET %S -> HTTP %lu", path, status);
    if (status == 200) {
        DWORD dwRead = 0; char buf[32768];
        while (WinHttpReadData(hReq, buf, sizeof(buf)-1, &dwRead) && dwRead > 0) { buf[dwRead]='\0'; result+=buf; }
        SD_LOG("body size = %zu bytes", result.size());
    } else {
        DWORD dwRead = 0; char buf[512]; std::string eb;
        while (WinHttpReadData(hReq, buf, sizeof(buf)-1, &dwRead) && dwRead > 0) { buf[dwRead]='\0'; eb+=buf; if(eb.size()>256)break; }
        SD_LOG("error body: %.200s", eb.c_str());
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return result;
}

// ---------------------------------------------------------------------------
// URL helpers
// ---------------------------------------------------------------------------
static std::string UrlDecode(const std::string& src) {
    std::string out; out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ) {
        if (src[i]=='%' && i+2<src.size()) { char h[3]={src[i+1],src[i+2],0}; out+=(char)(unsigned char)strtol(h,nullptr,16); i+=3; }
        else if (src[i]=='+') { out+=' '; ++i; }
        else { out+=src[i++]; }
    }
    return out;
}

static std::string QsGet(const std::string& qs, const std::string& key) {
    for (size_t start=0;;) {
        size_t pos = qs.find(key+"=", start);
        if (pos==std::string::npos) return "";
        bool boundary = (pos==0 || qs[pos-1]=='&' || qs[pos-1]=='?');
        if (boundary) {
            pos += key.size()+1;
            size_t end = qs.find('&', pos);
            return UrlDecode(end==std::string::npos ? qs.substr(pos) : qs.substr(pos,end-pos));
        }
        start = pos+1;
    }
}

// ---------------------------------------------------------------------------
// Step 1: player version from /iframe_api
// ---------------------------------------------------------------------------
static std::string GetPlayerVersion() {
    SD_LOG("fetching /iframe_api...");
    std::string body = HttpGet(L"www.youtube.com", L"/iframe_api");
    if (body.empty()) { SD_LOG("iframe_api returned empty body"); return ""; }
    std::regex re(R"(\\/s\\/player\\/([a-zA-Z0-9]+)\\/|/s/player/([a-zA-Z0-9]+)/)");
    std::smatch m;
    if (std::regex_search(body, m, re)) {
        std::string ver = m[1].matched ? m[1].str() : m[2].str();
        SD_LOG("player version = %s", ver.c_str());
        return ver;
    }
    SD_LOG("could not extract player version (body: %.120s)", body.c_str());
    return "";
}

static std::string PlayerJsPath(const std::string& ver) {
    return "/s/player/" + ver + "/player_ias.vflset/en_US/base.js";
}

// ---------------------------------------------------------------------------
// Step 2a: signatureTimestamp
// ---------------------------------------------------------------------------
static std::string ExtractSigTimestamp(const std::string& baseJs) {
    std::regex re(R"(signatureTimestamp[=:,](\d+))");
    std::smatch m;
    if (std::regex_search(baseJs, m, re)) { SD_LOG("signatureTimestamp = %s", m[1].str().c_str()); return m[1].str(); }
    SD_LOG("signatureTimestamp not found");
    return "";
}

// ---------------------------------------------------------------------------
// Step 2b: decipher ops
// ---------------------------------------------------------------------------
struct DecipherOp {
    enum Type { REVERSE, SPLICE, SWAP } type;
    int arg = 0;
};

static std::string ReEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::string(".^$*+?()[]{}|\\$").find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

static std::string ExtractFunctionBody(const std::string& js, const std::string& name) {
    std::string needle1 = name + "=function(";
    size_t pos = js.find(needle1);
    if (pos == std::string::npos) {
        std::string needle2 = name + ":function(";
        pos = js.find(needle2);
    }
    if (pos == std::string::npos) return "";
    size_t bracePos = js.find('{', pos);
    if (bracePos == std::string::npos) return "";
    int depth = 0; size_t end = std::string::npos;
    for (size_t i = bracePos; i < js.size(); ++i) {
        if (js[i]=='{') ++depth;
        else if (js[i]=='}') { if (--depth==0) { end=i; break; } }
    }
    if (end == std::string::npos) return "";
    return js.substr(bracePos+1, end-bracePos-1);
}

static std::vector<DecipherOp> ExtractOps(const std::string& js) {
    std::vector<DecipherOp> ops;

    // --- Step 1: find decipher function name ---
    //
    // Pattern priority (highest specificity first):
    //   0,1 : pytube -- encodeURIComponent call-site (modern players)
    //   2,3 : NewPipe -- decodeURIComponent &&(...) call-site
    //   4   : m=FUNC(decodeURIComponent(h.s))
    //   5   : c&&(c=FUNC(decodeURIComponent(c)))
    //   6,7 : split("") definition forms (last resort)
    std::string decipherName;
    {
        struct Pat { const char* re; int nameGroup; };
        static const Pat kPatterns[] = {
            // 0: c&&d.set(X,encodeURIComponent(FUNC(
            { R"(\b[cs]\s*&&\s*[a-zA-Z0-9_$]+\.set\([^,]+,\s*encodeURIComponent\s*\(\s*([a-zA-Z0-9$_]+)\()", 1 },
            // 1: broader &&X.set(X,encodeURIComponent(FUNC(
            { R"(\b[a-zA-Z0-9_$]+\s*&&\s*[a-zA-Z0-9_$]+\.set\([^,]+,\s*encodeURIComponent\s*\(\s*([a-zA-Z0-9$_]+)\()", 1 },
            // 2: &&(x=FUNC(N,decodeURIComponent(...)))
            { R"(\b(?:[a-zA-Z0-9_$]+)&&\((?:[a-zA-Z0-9_$]+)=([a-zA-Z0-9_$]{2,})\((?:\d+,)decodeURIComponent\((?:[a-zA-Z0-9_$]+)\)\))", 1 },
            // 3: &&(x=FUNC(decodeURIComponent(...)))
            { R"(\b(?:[a-zA-Z0-9_$]+)&&\((?:[a-zA-Z0-9_$]+)=([a-zA-Z0-9_$]{2,})\(decodeURIComponent\((?:[a-zA-Z0-9_$]+)\)\))", 1 },
            // 4: m=FUNC(decodeURIComponent(h.s))
            { R"(\bm=([a-zA-Z0-9$]{2,})\(decodeURIComponent\(h\.s\)\))", 1 },
            // 5: c&&(c=FUNC(decodeURIComponent(c)))
            { R"(\bc&&\(c=([a-zA-Z0-9$]{2,})\(decodeURIComponent\(c\)\))", 1 },
            // 6: word-boundary guarded split("") definition
            { R"((?:\b|[^a-zA-Z0-9$])([a-zA-Z0-9$]{2,})\s*=\s*function\(\s*a\s*\)\s*\{\s*a\s*=\s*a\.split\(\s*""\s*\))", 1 },
            // 7: backreference split("") form
            { R"(([\w$]+)\s*=\s*function\((\w+)\)\{\s*\2\s*=\s*\2\.split\(""\)\s*;)", 1 },
        };
        for (int pi = 0; pi < (int)(sizeof(kPatterns)/sizeof(kPatterns[0])); ++pi) {
            std::regex re(kPatterns[pi].re, std::regex::ECMAScript);
            std::smatch m;
            if (std::regex_search(js, m, re)) {
                std::string candidate = m[kPatterns[pi].nameGroup].str();
                if (!candidate.empty()) {
                    decipherName = candidate;
                    SD_LOG("decipher fn name (pattern %d): '%s'", pi, decipherName.c_str());
                    break;
                }
            }
        }
    }
    if (decipherName.empty()) {
        SD_LOG("could not find decipher function name");
        return ops;
    }

    // --- Step 2: extract function body ---
    std::string body = ExtractFunctionBody(js, decipherName);
    if (body.empty()) {
        SD_LOG("could not find body of function '%s'", decipherName.c_str());
        return ops;
    }
    SD_LOG("decipher body (first 150): %.150s", body.c_str());

    // --- Step 3: find helper object name ---
    std::string helperName;
    {
        std::regex rH1(R"(([a-zA-Z0-9$_]{1,5})\.[a-zA-Z0-9$_]{1,5}\()");
        std::regex rH2(R"([;,]([A-Za-z0-9_$]{2,})\[)");
        std::smatch m;
        if (std::regex_search(body, m, rH1)) {
            helperName = m[1].str();
        } else if (std::regex_search(body, m, rH2)) {
            helperName = m[1].str();
        } else {
            SD_LOG("helper object name not found in body");
            return ops;
        }
        SD_LOG("helper object = '%s'", helperName.c_str());
    }

    // --- Step 4: extract helper object body ---
    std::string objBody;
    {
        for (const std::string& needle : { "var "+helperName+"={", helperName+"={" }) {
            size_t pos = js.find(needle);
            if (pos == std::string::npos) continue;
            size_t bracePos = js.find('{', pos+needle.size()-1);
            if (bracePos == std::string::npos) continue;
            int depth=0; size_t end=std::string::npos;
            for (size_t i=bracePos; i<js.size(); ++i) {
                if (js[i]=='{') ++depth;
                else if (js[i]=='}') { if(--depth==0){end=i;break;} }
            }
            if (end != std::string::npos) { objBody=js.substr(bracePos+1,end-bracePos-1); break; }
        }
    }
    if (objBody.empty()) {
        SD_LOG("helper object body not found for '%s'", helperName.c_str());
        return ops;
    }
    SD_LOG("helper object body (first 200): %.200s", objBody.c_str());

    // --- Step 5: map helper fn names to op types ---
    //
    // Match by STRUCTURE, not by literal method names.
    // Modern players obfuscate "splice"/"reverse" as e[38]/e[39], so we
    // identify each op by what the function body does structurally:
    //
    //   REVERSE : function(z)    { z[ANYTHING]() }
    //             -- 1 param, body is a single method call with no args
    //             Literal fallback: a.reverse()
    //
    //   SPLICE  : function(z,D)  { z[ANYTHING](0,D) }
    //             -- 2 params, body calls something with (0,D)
    //             Literal fallback: a.splice(0,b)
    //
    //   SWAP    : function(z,D)  { var E=z[0];z[0]=z[D%...];z[D%...]=E }
    //             -- 2 params, classic swap pattern (unchanged by obfuscation)
    //
    std::map<std::string, DecipherOp::Type> fnMap;

    // SWAP -- structural pattern (invariant under obfuscation)
    {
        std::regex rSwp(
            R"(([a-zA-Z0-9$_]+)\s*:\s*function\(([a-zA-Z]),([a-zA-Z])\)\s*\{)"
            R"(\s*var\s+[a-zA-Z]\s*=\s*\2\[0\]\s*;\s*\2\[0\]\s*=\s*\2\[\3%\2[^]]*\]\s*;\s*\2\[\3%\2[^]]*\]\s*=[^}]+\})"
        );
        for (std::sregex_iterator it(objBody.begin(),objBody.end(),rSwp),e; it!=e; ++it) {
            fnMap[(*it)[1].str()] = DecipherOp::SWAP;
            SD_LOG("op SWAP (structural) -> fn '%s'", (*it)[1].str().c_str());
        }
    }

    // SPLICE -- structural: function(z,D){z[ANYTHING](0,D)} (obfuscated)
    {
        std::regex rSplOb(
            R"(([a-zA-Z0-9$_]+)\s*:\s*function\(([a-zA-Z]),([a-zA-Z])\)\s*\{)"
            R"(\s*\2\[[^\]]+\]\s*\(\s*0\s*,\s*\3\s*\)\s*\})"
        );
        for (std::sregex_iterator it(objBody.begin(),objBody.end(),rSplOb),e; it!=e; ++it) {
            if (fnMap.find((*it)[1].str()) == fnMap.end()) {
                fnMap[(*it)[1].str()] = DecipherOp::SPLICE;
                SD_LOG("op SPLICE (structural) -> fn '%s'", (*it)[1].str().c_str());
            }
        }
    }
    // SPLICE -- literal fallback: a.splice(0,b)
    {
        std::regex rSplLit(R"(([a-zA-Z0-9$_]+)\s*:\s*function\([a-zA-Z],[a-zA-Z]\)\{[^}]*\.splice\(0,[a-zA-Z]\)[^}]*\})");
        for (std::sregex_iterator it(objBody.begin(),objBody.end(),rSplLit),e; it!=e; ++it) {
            if (fnMap.find((*it)[1].str()) == fnMap.end()) {
                fnMap[(*it)[1].str()] = DecipherOp::SPLICE;
                SD_LOG("op SPLICE (literal) -> fn '%s'", (*it)[1].str().c_str());
            }
        }
    }

    // REVERSE -- structural: function(z){z[ANYTHING]()} single call no args (obfuscated)
    {
        std::regex rRevOb(
            R"(([a-zA-Z0-9$_]+)\s*:\s*function\(([a-zA-Z])\)\s*\{)"
            R"(\s*\2\[[^\]]+\]\s*\(\s*\)\s*\})"
        );
        for (std::sregex_iterator it(objBody.begin(),objBody.end(),rRevOb),e; it!=e; ++it) {
            if (fnMap.find((*it)[1].str()) == fnMap.end()) {
                fnMap[(*it)[1].str()] = DecipherOp::REVERSE;
                SD_LOG("op REVERSE (structural) -> fn '%s'", (*it)[1].str().c_str());
            }
        }
    }
    // REVERSE -- literal fallback: a.reverse()
    {
        std::regex rRevLit(R"(([a-zA-Z0-9$_]+)\s*:\s*function\([a-zA-Z]\)\{[^}]*\.reverse\(\)[^}]*\})");
        for (std::sregex_iterator it(objBody.begin(),objBody.end(),rRevLit),e; it!=e; ++it) {
            if (fnMap.find((*it)[1].str()) == fnMap.end()) {
                fnMap[(*it)[1].str()] = DecipherOp::REVERSE;
                SD_LOG("op REVERSE (literal) -> fn '%s'", (*it)[1].str().c_str());
            }
        }
    }

    if (fnMap.empty()) {
        SD_LOG("no ops found in helper object (first 300: %.300s)", objBody.c_str());
        return ops;
    }

    // --- Step 6: build op sequence from decipher body ---
    std::string reCallStr = ReEscape(helperName) + R"(\.([a-zA-Z0-9$_]+)\(([a-zA-Z])(?:,([0-9]+))?\))";
    std::regex rCall(reCallStr, std::regex::ECMAScript);
    for (std::sregex_iterator it(body.begin(),body.end(),rCall), end_it; it!=end_it; ++it) {
        auto found = fnMap.find((*it)[1].str());
        if (found==fnMap.end()) continue;
        DecipherOp op;
        op.type = found->second;
        std::string a = (*it)[3].str();
        op.arg = a.empty() ? 0 : std::stoi(a);
        ops.push_back(op);
    }
    SD_LOG("extracted %zu ops", ops.size());
    return ops;
}

static std::string ApplyOps(std::string sig, const std::vector<DecipherOp>& ops) {
    for (const auto& op : ops) {
        switch (op.type) {
        case DecipherOp::REVERSE: std::reverse(sig.begin(),sig.end()); break;
        case DecipherOp::SPLICE:
            if (op.arg>0 && op.arg<(int)sig.size()) sig=sig.substr(op.arg);
            break;
        case DecipherOp::SWAP: {
            int idx=op.arg%(int)sig.size();
            if (idx>0) std::swap(sig[0],sig[idx]);
            break;
        }}
    }
    return sig;
}

// ---------------------------------------------------------------------------
// Session cache
// ---------------------------------------------------------------------------
static std::vector<DecipherOp> s_ops;
static std::string              s_playerVer;
static std::string              s_sigTimestamp;
static bool                     s_ready = false;

inline void InvalidateCache() { s_ready=false; s_ops.clear(); s_sigTimestamp.clear(); }
inline std::string GetSigTimestamp() { return s_sigTimestamp; }

inline bool EnsureLoaded() {
    if (s_ready) return true;
    SD_LOG("EnsureLoaded: starting...");
    std::string ver = GetPlayerVersion();
    if (ver.empty()) { SD_LOG("EnsureLoaded: GetPlayerVersion returned empty"); return false; }
    if (ver==s_playerVer && !s_ops.empty()) {
        SD_LOG("EnsureLoaded: cache hit (ver=%s)", ver.c_str());
        s_ready=true; return true;
    }
    std::string path = PlayerJsPath(ver);
    SD_LOG("EnsureLoaded: downloading %s", path.c_str());
    std::wstring wp(path.begin(), path.end());
    std::string baseJs = HttpGet(L"www.youtube.com", wp.c_str());
    if (baseJs.empty()) { SD_LOG("EnsureLoaded: base.js download failed"); return false; }
    SD_LOG("EnsureLoaded: base.js size = %zu bytes", baseJs.size());
    auto ops = ExtractOps(baseJs);
    if (ops.empty()) { SD_LOG("EnsureLoaded: ExtractOps returned 0 ops"); return false; }
    s_ops          = std::move(ops);
    s_playerVer    = ver;
    s_sigTimestamp = ExtractSigTimestamp(baseJs);
    s_ready        = true;
    SD_LOG("EnsureLoaded: ready (sigTs=%s)", s_sigTimestamp.c_str());
    return true;
}

inline std::string DecodeCipher(const std::string& cipherValue) {
    std::string rawUrl = QsGet(cipherValue, "url");
    std::string s      = QsGet(cipherValue, "s");
    std::string sp     = QsGet(cipherValue, "sp");
    if (rawUrl.empty() || s.empty()) { SD_LOG("DecodeCipher: missing url or s field"); return ""; }
    if (sp.empty()) sp = "signature";
    SD_LOG("DecodeCipher: raw s (first 40): %.40s", s.c_str());
    if (!EnsureLoaded()) return "";
    std::string decoded = ApplyOps(s, s_ops);
    SD_LOG("DecodeCipher: decoded (first 40): %.40s", decoded.c_str());
    std::string finalUrl = rawUrl;
    finalUrl += (finalUrl.find('?')==std::string::npos) ? '?' : '&';
    finalUrl += sp + "=" + decoded;
    SD_LOG("DecodeCipher: final URL (first 80): %.80s", finalUrl.c_str());
    return finalUrl;
}

} // namespace SigDecipher

#undef SD_LOG
