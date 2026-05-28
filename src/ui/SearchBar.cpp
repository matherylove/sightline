#include "SearchBar.h"
static HWND s_hNotify = NULL;
static WNDPROC s_origProc = NULL;
static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(s_hNotify, WM_COMMAND, MAKEWPARAM(102, BN_CLICKED), 0);
        return 0;
    }
    return CallWindowProc(s_origProc, hWnd, msg, wParam, lParam);
}
void SearchBar::Subclass(HWND hEdit, HWND hParentToNotify) {
    s_hNotify  = hParentToNotify;
    s_origProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
}
