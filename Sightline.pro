QT += core gui widgets network
CONFIG += c++11
TARGET = Sightline
TEMPLATE = app

# Windows XP needs the older subsystem version stamped into the PE header,
# and MSVC 2017 only produces XP-compatible binaries with the v141_xp
# toolset plus the 7.1A SDK, which the CI workflow sets up.
win32-msvc* {
    QMAKE_LFLAGS_WINDOWS += /SUBSYSTEM:WINDOWS,5.01
    QMAKE_CXXFLAGS += /utf-8
    DEFINES += _WIN32_WINNT=0x0501 WINVER=0x0501
}

win32-g++ {
    QMAKE_LFLAGS += -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,1
    DEFINES += _WIN32_WINNT=0x0501 WINVER=0x0501
}

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/main_window.cpp \
    src/media_types.cpp \
    src/app_settings.cpp \
    src/sightline_paths.cpp \
    src/sightline_paint.cpp \
    src/sightline_window.cpp \
    src/library.cpp \
    src/listening_stats.cpp \
    src/sponsorblock.cpp \
    src/ytdlp.cpp \
    src/playback.cpp \
    src/widgets.cpp \
    src/player_page.cpp \
    src/music_page.cpp \
    src/stats_page.cpp \
    src/dialogs.cpp \
    src/pip_window.cpp

HEADERS += \
    src/main_window.h \
    src/media_types.h \
    src/app_settings.h \
    src/sightline_paths.h \
    src/sightline_paint.h \
    src/sightline_style.h \
    src/sightline_window.h \
    src/library.h \
    src/listening_stats.h \
    src/sponsorblock.h \
    src/ytdlp.h \
    src/playback.h \
    src/widgets.h \
    src/player_page.h \
    src/music_page.h \
    src/stats_page.h \
    src/dialogs.h \
    src/pip_window.h

RESOURCES += resources.qrc

win32:RC_FILE = sightline.rc
