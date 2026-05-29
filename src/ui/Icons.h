#pragma once
// Icons.h — Font Awesome 6 Free Solid codepoints used in Sightline
// Font file: assets/fonts/fa-solid-900.ttf (FA6 Free, OFL-1.1 licence)
// Usage:  ImGui::Button(ICON_FA_PLAY "##id");
//         ImGui::Text(ICON_FA_VOLUME_HIGH);

// Glyph range to request when merging: 0xE000..0xF8FF is private-use;
// FA6 uses the ranges below — request them in the ImFontConfig.

#define ICON_FA_HOUSE           "\xef\x80\x95"   // f015
#define ICON_FA_MAGNIFYING_GLASS "\xef\x80\x82"  // f002
#define ICON_FA_BACKWARD_STEP   "\xef\x91\xa0"   // f048
#define ICON_FA_BACKWARD        "\xef\x81\x8a"   // f04a  (<<)
#define ICON_FA_PLAY            "\xef\x81\x8b"   // f04b
#define ICON_FA_PAUSE           "\xef\x81\x8c"   // f04c
#define ICON_FA_FORWARD         "\xef\x81\x8e"   // f04e  (>>)
#define ICON_FA_FORWARD_STEP    "\xef\x91\xa1"   // f051
#define ICON_FA_DOWNLOAD        "\xef\x80\x99"   // f019
#define ICON_FA_HEART           "\xef\x80\x84"   // f004
#define ICON_FA_HEART_CRACK     "\xef\x9a\x99"   // f7a9 (un-fav)
#define ICON_FA_WINDOW_RESTORE  "\xef\x85\xad"   // f2d2
#define ICON_FA_EXPAND          "\xef\x87\x9e"   // f1de -> use arrows-alt f0b2
#define ICON_FA_COMPRESS        "\xef\x90\xa6"   // f422 -> compress f066
#define ICON_FA_VOLUME_HIGH     "\xef\x80\xa8"   // f028
#define ICON_FA_VOLUME_LOW      "\xef\x80\xa7"   // f027
#define ICON_FA_VOLUME_XMARK    "\xef\x9a\xa9"   // f6a9
#define ICON_FA_BELL            "\xef\x83\xb3"   // f0f3
#define ICON_FA_FIRE            "\xef\x80\x9f"   // f06d  (trending)
#define ICON_FA_RSS             "\xef\x82\x9e"   // f09e  (subscriptions)
#define ICON_FA_STAR            "\xef\x80\x85"   // f005  (favourites)
#define ICON_FA_CLOCK_ROTATE_LEFT "\xef\x87\x9a" // f1da  (history)
#define ICON_FA_CIRCLE_DOWN     "\xef\x95\x98"   // f358  (downloads)
#define ICON_FA_GEAR            "\xef\x80\x93"   // f013  (settings)
#define ICON_FA_ARROWS_ALT      "\xef\x82\xb2"   // f0b2  (fullscreen)
#define ICON_FA_COMPRESS_ALT    "\xef\x90\xa1"   // f422

// Glyph ranges to pass to AddFontFromFileTTF when merging:
// static const ImWchar kIconRanges[] = { 0xf000, 0xf8ff, 0 };
