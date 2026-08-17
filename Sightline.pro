QT += core gui widgets network
CONFIG += c++11
TARGET = Sightline
TEMPLATE = app

# ---------------------------------------------------------------------------
# Windows XP target
#
# MSVC 2017 only produces XP-compatible binaries with the v141_xp toolset, and
# the PE subsystem version has to be stamped down to 5.1 by hand or the loader
# on XP refuses the file outright.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# FFmpeg 7.1 (N-116828-g6aafe61-Reino, XP mod, SSE)
#
# Linked dynamically on purpose: the DLLs sit beside the executable and can be
# swapped for a newer XP build without rebuilding Sightline. avcodec 61,
# avformat 61, avutil 59, swscale 8, swresample 5.
# ---------------------------------------------------------------------------
FFMPEG_DIR = $$PWD/third_party/ffmpeg
INCLUDEPATH += $$FFMPEG_DIR/include

win32-msvc* {
    LIBS += -L$$FFMPEG_DIR/lib \
        -lavcodec -lavformat -lavutil -lswscale -lswresample
    # DirectSound is the only push-model audio API on XP: no WASAPI, and
    # waveOut gives no play cursor for the A/V clock to read.
    LIBS += -ldsound -lole32 -luser32
}

win32-g++ {
    LIBS += -L$$FFMPEG_DIR/lib \
        -lavcodec -lavformat -lavutil -lswscale -lswresample \
        -ldsound -lole32 -luser32
}

unix {
    # Developer convenience only; the shipping target is Windows XP.
    CONFIG += link_pkgconfig
    PKGCONFIG += libavcodec libavformat libavutil libswscale libswresample
}

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
    src/ytdlp_setup.cpp \
    src/net_transport.cpp \
    src/thumbnail_fetcher.cpp \
    src/media_source.cpp \
    src/media_decoder.cpp \
    src/audio_sink.cpp \
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
    src/ytdlp_setup.h \
    src/net_transport.h \
    src/thumbnail_fetcher.h \
    src/media_source.h \
    src/media_decoder.h \
    src/audio_sink.h \
    src/playback.h \
    src/widgets.h \
    src/player_page.h \
    src/music_page.h \
    src/stats_page.h \
    src/dialogs.h \
    src/pip_window.h

RESOURCES += resources.qrc

win32:RC_FILE = sightline.rc
