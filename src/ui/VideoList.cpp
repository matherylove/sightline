#include "VideoList.h"
#include <commctrl.h>

std::vector<VideoItem> VideoList::s_items;

HWND VideoList::Create(HWND hParent, HINSTANCE hInst, int id, int x, int y, int w, int h) {
    return CreateWindowEx(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        x, y, w, h, hParent, (HMENU)(INT_PTR)id, hInst, NULL);
}

void VideoList::Clear(HWND hList) {
    if (hList) ListView_DeleteAllItems(hList);
    s_items.clear();
}

void VideoList::AddItem(HWND hList, const VideoItem& item) {
    s_items.push_back(item);
    int idx = (int)s_items.size() - 1;
    LVITEM lvi = {};
    lvi.mask     = LVIF_TEXT;
    lvi.iItem    = idx;
    lvi.pszText  = (LPWSTR)item.title.c_str();
    ListView_InsertItem(hList, &lvi);
    ListView_SetItemText(hList, idx, 1, (LPWSTR)item.channel.c_str());
    ListView_SetItemText(hList, idx, 2, (LPWSTR)item.duration.c_str());
    ListView_SetItemText(hList, idx, 3, (LPWSTR)item.views.c_str());
}

VideoItem VideoList::GetItem(HWND hList, int index) {
    if (index < 0 || index >= (int)s_items.size()) return {};
    return s_items[index];
}
