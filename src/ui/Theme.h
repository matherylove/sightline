#pragma once
#include "imgui.h"

namespace Theme {
    // Sightline Visualizer dark teal palette
    //   --bg:           #0E1114
    //   --surface:      #151A1F
    //   --surface-2:    #1B2228
    //   --border:       #2A333B
    //   --text:         #E8EEF2
    //   --text-muted:   #A8B3BC
    //   --text-faint:   #6F7A83
    //   --accent:       #4FA3A3
    //   --accent-hover: #66B8B8
    //   --accent-soft:  #1E3E42
    //   --error:        #C96B6B
    //   --warning:      #C9A96B

    // Helper: convert 0-255 ints to 0-1 float
    static const ImVec4 COL_BG            = {0.055f, 0.067f, 0.078f, 1.0f}; // #0E1114
    static const ImVec4 COL_CARD          = {0.082f, 0.102f, 0.122f, 1.0f}; // #151A1F
    static const ImVec4 COL_SURFACE2      = {0.106f, 0.133f, 0.157f, 1.0f}; // #1B2228
    static const ImVec4 COL_CONTRAST_V4   = {0.165f, 0.200f, 0.231f, 1.0f}; // #2A333B
    static const ImVec4 COL_ACCENT_V4     = {0.310f, 0.639f, 0.639f, 1.0f}; // #4FA3A3
    static const ImVec4 COL_ACCENT_HOV_V4 = {0.400f, 0.722f, 0.722f, 1.0f}; // #66B8B8
    static const ImVec4 COL_ACCENT_SOFT   = {0.118f, 0.243f, 0.259f, 1.0f}; // #1E3E42
    static const ImVec4 COL_TEXT          = {0.910f, 0.933f, 0.949f, 1.0f}; // #E8EEF2  — primary text
    static const ImVec4 COL_TEXT_DIM_V4   = {0.659f, 0.702f, 0.737f, 1.0f}; // #A8B3BC  — secondary (channel names, labels)
    static const ImVec4 COL_TEXT_FAINT    = {0.435f, 0.478f, 0.514f, 1.0f}; // #6F7A83  — tertiary (views, duration, meta)
    static const ImVec4 COL_SELECTED      = {0.310f, 0.639f, 0.639f, 0.18f}; // accent @ 18% alpha
    static const ImVec4 COL_ERROR         = {0.788f, 0.420f, 0.420f, 1.0f}; // #C96B6B
    static const ImVec4 COL_WARNING       = {0.788f, 0.663f, 0.420f, 1.0f}; // #C9A96B

    // For legacy code using ImU32
    inline ImU32 U32_BG()     { return ImGui::ColorConvertFloat4ToU32(COL_BG);       }
    inline ImU32 U32_CARD()   { return ImGui::ColorConvertFloat4ToU32(COL_CARD);     }
    inline ImU32 U32_ACCENT() { return ImGui::ColorConvertFloat4ToU32(COL_ACCENT_V4); }
}
