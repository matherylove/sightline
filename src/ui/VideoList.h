#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct VideoItem {
    std::string  videoId;
    std::wstring title;
    std::wstring channel;
    std::wstring duration;
    std::wstring views;
    std::string  thumbnailUrl;  // https://img.youtube.com/vi/<id>/hqdefault.jpg
};
