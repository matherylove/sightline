#pragma once
#include <windows.h>
// Reserved for future embedded player view (MPC-HC /slave child window)
namespace PlayerView {
    void AttachMPCWindow(HWND hMPCWnd, HWND hContainer);
    void Detach();
}
