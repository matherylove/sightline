#pragma once
// ThumbnailCache - async YouTube thumbnail downloader for D3D9 / ImGui
// No D3DX dependency. Uses stb_image for JPEG decode + raw D3D9 CreateTexture.
//
// Thumbnail URL: img.youtube.com/vi/{videoId}/hqdefault.jpg
//
// stbi_* symbols are defined in stb_image_impl.cpp (compiled exactly once).
// s_instance is declared extern here and defined in stb_image_impl.cpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <d3d9.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

// stb_image forward-declarations (implementation in stb_image_impl.cpp)
typedef unsigned char stbi_uc;
extern "C" stbi_uc* stbi_load_from_memory(const stbi_uc* buf, int len,
    int* x, int* y, int* ch, int desired);
extern "C" void stbi_image_free(void* p);

struct ThumbEntry {
    enum State { PENDING, LOADING, READY, FAILED };
    State              state   = PENDING;
    IDirect3DTexture9* texture = nullptr;
};

struct ThumbTask {
    std::string                videoId;
    std::vector<unsigned char> data;
    bool                       failed = false;
};

class ThumbnailCache {
public:
    ThumbnailCache()  = default;
    ~ThumbnailCache() { Clear(); }

    void Init(IDirect3DDevice9* dev) { m_dev = dev; }

    // Call once per frame (before ImGui::NewFrame)
    void Tick() {
        std::vector<ThumbTask*> done;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            done.swap(m_done);
        }
        for (ThumbTask* task : done) {
            auto it = m_cache.find(task->videoId);
            if (it != m_cache.end()) {
                if (!task->failed && !task->data.empty() && m_dev) {
                    IDirect3DTexture9* tex = _UploadJpeg(task->data);
                    it->second.state   = tex ? ThumbEntry::READY : ThumbEntry::FAILED;
                    it->second.texture = tex;
                } else {
                    it->second.state = ThumbEntry::FAILED;
                }
            }
            delete task;
        }
    }

    // Returns texture if ready, nullptr otherwise (triggers download on first call)
    IDirect3DTexture9* Get(const std::string& videoId) {
        auto it = m_cache.find(videoId);
        if (it == m_cache.end()) {
            ThumbEntry e; e.state = ThumbEntry::LOADING;
            m_cache[videoId] = e;
            _Enqueue(videoId);
            return nullptr;
        }
        return (it->second.state == ThumbEntry::READY) ? it->second.texture : nullptr;
    }

    // Called before D3D9 Reset — releases all GPU textures but keeps videoId keys.
    // Entries transition to FAILED so OnDeviceReset can re-enqueue them.
    void OnDeviceLost() {
        for (auto& kv : m_cache) {
            if (kv.second.texture) {
                kv.second.texture->Release();
                kv.second.texture = nullptr;
            }
            if (kv.second.state == ThumbEntry::READY)
                kv.second.state = ThumbEntry::FAILED;
        }
    }

    // Called after a successful D3D9 Reset — updates the device pointer and
    // re-enqueues every entry that lost its texture so thumbnails reload
    // automatically after a window resize or device reset.
    void OnDeviceReset(IDirect3DDevice9* dev) {
        m_dev = dev;
        for (auto& kv : m_cache) {
            if (kv.second.state == ThumbEntry::FAILED && kv.second.texture == nullptr) {
                kv.second.state = ThumbEntry::LOADING;
                _Enqueue(kv.first);
            }
        }
    }

    void Clear() { OnDeviceLost(); m_cache.clear(); }

    // Singleton — defined once in stb_image_impl.cpp
    static ThumbnailCache* s_instance;

private:
    IDirect3DDevice9*                            m_dev = nullptr;
    std::unordered_map<std::string, ThumbEntry>  m_cache;
    std::mutex                                   m_mutex;
    std::vector<ThumbTask*>                      m_done;

    IDirect3DTexture9* _UploadJpeg(const std::vector<unsigned char>& buf) {
        int w = 0, h = 0, ch = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            buf.data(), (int)buf.size(), &w, &h, &ch, 4);
        if (!pixels) return nullptr;

        IDirect3DTexture9* tex = nullptr;
        HRESULT hr = m_dev->CreateTexture(
            (UINT)w, (UINT)h, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
        if (FAILED(hr)) { stbi_image_free(pixels); return nullptr; }

        D3DLOCKED_RECT lr;
        if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
            tex->Release(); stbi_image_free(pixels); return nullptr;
        }
        // RGBA -> ARGB (swap R and B channels for D3D9)
        auto* dst = (unsigned char*)lr.pBits;
        for (int y = 0; y < h; y++) {
            auto* row = dst + y * lr.Pitch;
            auto* src = pixels + y * w * 4;
            for (int x = 0; x < w; x++) {
                row[x*4+0] = src[x*4+2]; // B
                row[x*4+1] = src[x*4+1]; // G
                row[x*4+2] = src[x*4+0]; // R
                row[x*4+3] = src[x*4+3]; // A
            }
        }
        tex->UnlockRect(0);
        stbi_image_free(pixels);
        return tex;
    }

    void _Enqueue(const std::string& videoId) {
        ThumbTask* task = new ThumbTask;
        task->videoId = videoId;
        HANDLE h = CreateThread(nullptr, 0, _Worker, task, 0, nullptr);
        if (h) CloseHandle(h);
        else {
            task->failed = true;
            std::lock_guard<std::mutex> lk(m_mutex);
            m_done.push_back(task);
        }
    }

    // Downloads hqdefault.jpg from img.youtube.com
    static std::vector<unsigned char> _Download(const std::string& videoId) {
        std::vector<unsigned char> buf;

        std::wstring path = L"/vi/";
        for (char c : videoId) path += (wchar_t)(unsigned char)c;
        path += L"/hqdefault.jpg";

        HINTERNET hSess = WinHttpOpen(
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSess) return buf;

        DWORD secProto =
            WINHTTP_FLAG_SECURE_PROTOCOL_SSL3   |
            WINHTTP_FLAG_SECURE_PROTOCOL_TLS1   |
            WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
            WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(hSess, WINHTTP_OPTION_SECURE_PROTOCOLS, &secProto, sizeof(secProto));
        WinHttpSetTimeouts(hSess, 10000, 10000, 15000, 15000);

        HINTERNET hConn = WinHttpConnect(hSess, L"img.youtube.com",
            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConn) { WinHttpCloseHandle(hSess); return buf; }

        HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!hReq) {
            WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSess);
            return buf;
        }

        if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(hReq, nullptr))
        {
            DWORD status = 0, ssz = sizeof(DWORD);
            WinHttpQueryHeaders(hReq,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                nullptr, &status, &ssz, nullptr);
            if (status == 200) {
                DWORD dwRead = 0; char tmp[8192];
                while (WinHttpReadData(hReq, tmp, sizeof(tmp), &dwRead) && dwRead > 0)
                    buf.insert(buf.end(), tmp, tmp + dwRead);
            }
        }

        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        return buf;
    }

    static DWORD WINAPI _Worker(LPVOID param) {
        ThumbTask* task = (ThumbTask*)param;
        task->data   = _Download(task->videoId);
        task->failed = task->data.empty();
        if (s_instance) {
            std::lock_guard<std::mutex> lk(s_instance->m_mutex);
            s_instance->m_done.push_back(task);
        } else {
            delete task;
        }
        return 0;
    }
};
