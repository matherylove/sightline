#include "os_capabilities.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

extern "C" {
#include <libavutil/hwcontext.h>
}

OsCapabilities::Version OsCapabilities::version_ = OsCapabilities::Unknown;
bool OsCapabilities::probed_ = false;
bool OsCapabilities::ffmpegDxva2_ = false;

bool OsCapabilities::isWindows()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void OsCapabilities::probe()
{
    if (probed_)
        return;
    probed_ = true;

#ifdef Q_OS_WIN
    // GetVersionEx is deprecated and lies on Windows 8.1 and later without a
    // manifest, but the only distinction this needs is "Vista or newer", and
    // for that the lie is harmless: a shimmed 6.2 is still well past Vista.
    OSVERSIONINFOEXW info;
    ZeroMemory(&info, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);

#pragma warning(push)
#pragma warning(disable: 4996)
    if (GetVersionExW(reinterpret_cast<OSVERSIONINFOW *>(&info))) {
#pragma warning(pop)
        const int major = int(info.dwMajorVersion);
        const int minor = int(info.dwMinorVersion);

        if (major >= 10)                       version_ = Windows10OrLater;
        else if (major == 6 && minor >= 3)     version_ = Windows81;
        else if (major == 6 && minor == 2)     version_ = Windows8;
        else if (major == 6 && minor == 1)     version_ = Windows7;
        else if (major == 6 && minor == 0)     version_ = WindowsVista;
        else if (major == 5)                   version_ = WindowsXp;
        else                                   version_ = Unknown;
    }

    // A build shimmed down to XP compatibility still reports 5.1 here, which
    // is the right answer: if the loader is pretending, the APIs behave that
    // way too and gating on the pretence is safer than gating on the truth.
#else
    version_ = Unknown;
#endif

    ffmpegDxva2_ = (av_hwdevice_find_type_by_name("dxva2") != AV_HWDEVICE_TYPE_NONE);
}

OsCapabilities::Version OsCapabilities::version()
{
    probe();
    return version_;
}

bool OsCapabilities::atLeast(Version required)
{
    return int(version()) >= int(required);
}

QString OsCapabilities::versionName()
{
    switch (version()) {
    case WindowsXp:         return QString::fromLatin1("Windows XP");
    case WindowsVista:      return QString::fromLatin1("Windows Vista");
    case Windows7:          return QString::fromLatin1("Windows 7");
    case Windows8:          return QString::fromLatin1("Windows 8");
    case Windows81:         return QString::fromLatin1("Windows 8.1");
    case Windows10OrLater:  return QString::fromLatin1("Windows 10 o posterior");
    default:                break;
    }
    return QString::fromUtf8("Windows desconocido");
}

bool OsCapabilities::supportsDxva2()
{
    return atLeast(WindowsVista);
}

bool OsCapabilities::ffmpegHasDxva2()
{
    probe();
    return ffmpegDxva2_;
}

bool OsCapabilities::hardwareDecodingAvailable()
{
    return supportsDxva2() && ffmpegHasDxva2();
}

QString OsCapabilities::hardwareDecodingSummary()
{
    if (!supportsDxva2()) {
        return QString::fromUtf8(
            "No disponible en %1: DXVA2 necesita WDDM, que llegó con Vista. "
            "La decodificación va por CPU.").arg(versionName());
    }
    if (!ffmpegHasDxva2()) {
        return QString::fromUtf8(
            "Esta compilación de FFmpeg no trae DXVA2. La decodificación va por CPU.");
    }
    return QString::fromUtf8("DXVA2 disponible en %1.").arg(versionName());
}
