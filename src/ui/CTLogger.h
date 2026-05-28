#pragma once
// CTLogger.h  --  Debug console window (Win32 AllocConsole)
//
// Thread-safety fix: InitializeCriticalSection is called exactly once inside
// Enable(true), before any worker threads are spawned. The old lazy Init()
// inside LogC/Log was racy — two threads could both see s_inited==false and
// double-initialize the CRITICAL_SECTION, corrupting it silently.

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>

namespace CTLogger {

namespace detail {
    static HANDLE            s_hOut    = INVALID_HANDLE_VALUE;
    static bool              s_enabled = false;
    static CRITICAL_SECTION  s_cs;
    // cs_ready is set to true only after InitializeCriticalSection succeeds.
    // Worker threads check this before calling Enter/LeaveCriticalSection.
    static volatile bool     s_csReady = false;

    inline void RedirectHandles() {
        s_hOut = CreateFileA(
            "CONOUT$",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (s_hOut != INVALID_HANDLE_VALUE)
            SetStdHandle(STD_OUTPUT_HANDLE, s_hOut);
        FILE* fp = NULL;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
} // namespace detail

inline void Enable(bool on) {
    detail::s_enabled = on;

    if (on) {
        if (detail::s_hOut == INVALID_HANDLE_VALUE) {
            // Initialize CS exactly once here, before any threads are created
            if (!detail::s_csReady) {
                InitializeCriticalSection(&detail::s_cs);
                detail::s_csReady = true;
            }

            AllocConsole();
            SetConsoleTitleA("ClientTube -- Debug Log");
            detail::RedirectHandles();

            CONSOLE_SCREEN_BUFFER_INFO cbi;
            if (GetConsoleScreenBufferInfo(detail::s_hOut, &cbi)) {
                COORD sz = { 200, 5000 };
                SetConsoleScreenBufferSize(detail::s_hOut, sz);
                SMALL_RECT wr = { 0, 0, 199, 48 };
                SetConsoleWindowInfo(detail::s_hOut, TRUE, &wr);
            }
            SetConsoleTextAttribute(detail::s_hOut,
                FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

            const char* hdr =
                "========================================================\r\n"
                "  ClientTube  |  Debug Console  (v0.1.0)\r\n"
                "========================================================\r\n";
            DWORD w;
            WriteConsoleA(detail::s_hOut, hdr, (DWORD)strlen(hdr), &w, NULL);
        } else {
            HWND hw = GetConsoleWindow();
            if (hw) ShowWindow(hw, SW_SHOW);
        }
    } else {
        HWND hw = GetConsoleWindow();
        if (hw) ShowWindow(hw, SW_HIDE);
    }
}

inline bool IsEnabled() { return detail::s_enabled; }

inline void Log(const char* fmt, ...) {
    if (!detail::s_enabled || detail::s_hOut == INVALID_HANDLE_VALUE) return;
    if (!detail::s_csReady) return;
    EnterCriticalSection(&detail::s_cs);

    char buf[2048];
    va_list va; va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') {
        if (len < 2 || buf[len-2] != '\r') {
            if (len < sizeof(buf)-2) { buf[len-1] = '\r'; buf[len] = '\n'; buf[len+1] = '\0'; len++; }
        }
    } else if (len < sizeof(buf)-2) {
        buf[len] = '\r'; buf[len+1] = '\n'; buf[len+2] = '\0'; len += 2;
    }

    SYSTEMTIME st; GetLocalTime(&st);
    char ts[32];
    snprintf(ts, sizeof(ts), "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    DWORD w;
    SetConsoleTextAttribute(detail::s_hOut, FOREGROUND_GREEN | FOREGROUND_BLUE);
    WriteConsoleA(detail::s_hOut, ts, (DWORD)strlen(ts), &w, NULL);
    SetConsoleTextAttribute(detail::s_hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    WriteConsoleA(detail::s_hOut, buf, (DWORD)len, &w, NULL);

    LeaveCriticalSection(&detail::s_cs);
}

inline void LogC(char cat, const char* fmt, ...) {
    if (!detail::s_enabled || detail::s_hOut == INVALID_HANDLE_VALUE) return;
    if (!detail::s_csReady) return;
    EnterCriticalSection(&detail::s_cs);

    char buf[2048];
    va_list va; va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') {
        if (len < 2 || buf[len-2] != '\r') {
            if (len < sizeof(buf)-2) { buf[len-1] = '\r'; buf[len] = '\n'; buf[len+1] = '\0'; len++; }
        }
    } else if (len < sizeof(buf)-2) {
        buf[len] = '\r'; buf[len+1] = '\n'; buf[len+2] = '\0'; len += 2;
    }

    SYSTEMTIME st; GetLocalTime(&st);
    char prefix[48];
    snprintf(prefix, sizeof(prefix), "[%02d:%02d:%02d][%c] ",
        st.wHour, st.wMinute, st.wSecond, cat);

    WORD catColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    switch (cat) {
    case 'W': catColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
    case 'E': catColor = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
    case 'D': catColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
    case 'I': catColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
    }

    DWORD w;
    SetConsoleTextAttribute(detail::s_hOut, catColor);
    WriteConsoleA(detail::s_hOut, prefix, (DWORD)strlen(prefix), &w, NULL);
    SetConsoleTextAttribute(detail::s_hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    WriteConsoleA(detail::s_hOut, buf, (DWORD)len, &w, NULL);

    LeaveCriticalSection(&detail::s_cs);
}

} // namespace CTLogger
