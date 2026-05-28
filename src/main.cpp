#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "ui/MainWindow.h"
#include "ui/CTLogger.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    CTLogger::Enable(false);
    CTLogger::LogC('I', "[main] ClientTube starting up...");

    MainWindow win(hInst);
    if (!win.Create()) {
        MessageBoxW(NULL, L"Could not create the main window.",
            L"ClientTube", MB_ICONERROR);
        return 1;
    }
    win.Run();   // shows the window and runs the message loop
    return 0;
}
