cmake_minimum_required(VERSION 3.10)
project(ClientTube VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# XP SP3 compatible target
add_definitions(-D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -D_UNICODE -DUNICODE)

# ---- libVLC SDK (flexible path) --------------------------------------------
# Option 1: pass -DVLC_SDK_ROOT=<path> to cmake
# Option 2: install VLC to Program Files (auto-detected below)
# Option 3: CI downloads the 7z and sets VLC_SDK_ROOT automatically
if(NOT DEFINED VLC_SDK_ROOT OR VLC_SDK_ROOT STREQUAL "")
    foreach(_try
            "C:/Program Files/VideoLAN/VLC"
            "C:/Program Files (x86)/VideoLAN/VLC"
            "$ENV{ProgramFiles}/VideoLAN/VLC"
            "$ENV{ProgramFiles(x86)}/VideoLAN/VLC")
        if(EXISTS "${_try}/sdk/include/vlc/vlc.h")
            set(VLC_SDK_ROOT "${_try}")
            break()
        endif()
    endforeach()
endif()

if(NOT EXISTS "${VLC_SDK_ROOT}/sdk/include/vlc/vlc.h")
    message(FATAL_ERROR
        "libVLC SDK not found.\n"
        "Download VLC win32 package and pass -DVLC_SDK_ROOT=<extracted_path> to cmake,\n"
        "or install VLC to Program Files.")
endif()

message(STATUS "Using VLC SDK: ${VLC_SDK_ROOT}")
set(VLC_INCLUDE_DIR "${VLC_SDK_ROOT}/sdk/include")
set(VLC_LIB_DIR     "${VLC_SDK_ROOT}/sdk/lib")

# ---- ImGui sources ---------------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
)

set(SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/stb_image_impl.cpp
    src/extractor/InnerTube.cpp
    src/player/PlayerLauncher.cpp
    src/player/VLCPlayer.cpp
    src/config/Config.cpp
    ${IMGUI_SOURCES}
    res/resources.rc
)

add_executable(ClientTube WIN32 ${SOURCES})

target_include_directories(ClientTube PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    third_party
    ${VLC_INCLUDE_DIR}
)

target_link_directories(ClientTube PRIVATE ${VLC_LIB_DIR})

target_link_libraries(ClientTube
    libvlc
    libvlccore
    d3d9
    ole32 oleaut32 uuid
    winhttp ws2_32
    urlmon
    comctl32
    shlwapi comdlg32 shell32 dwmapi
    gdi32 user32 advapi32
)

if(MSVC)
    target_compile_options(ClientTube PRIVATE /O2 /W3)
else()
    target_compile_options(ClientTube PRIVATE -O2 -Wall)
    # Embed ALL MinGW runtime libs (libgcc, libstdc++, libwinpthread)
    # so the .exe has no external DLL dependencies from the toolchain.
    target_link_options(ClientTube PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()
