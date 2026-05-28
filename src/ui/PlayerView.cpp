#include "PlayerView.h"

void PlayerView::AttachMPCWindow(HWND hMPCWnd, HWND hContainer) {
    // Reparent MPC-HC window into our container
    SetParent(hMPCWnd, hContainer);
    RECT rc;
    GetClientRect(hContainer, &rc);
    SetWindowPos(hMPCWnd, HWND_TOP, 0, 0,
        rc.right, rc.bottom, SWP_SHOWWINDOW);
    // Remove title bar from MPC-HC window style
    LONG style = GetWindowLong(hMPCWnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_OVERLAPPEDWINDOW);
    SetWindowLong(hMPCWnd, GWL_STYLE, style);
    SetWindowPos(hMPCWnd, NULL, 0, 0,
        rc.right, rc.bottom, SWP_FRAMECHANGED | SWP_NOZORDER);
}

void PlayerView::Detach() {
    // Nothing needed — process exit handles cleanup
}
