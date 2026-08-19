#ifndef SIGHTLINE_OS_CAPABILITIES_H
#define SIGHTLINE_OS_CAPABILITIES_H

#include <QString>

// What this particular Windows can actually do.
//
// Sightline targets XP, but nothing stops it running on something newer, and
// a few things that are impossible on XP are ordinary elsewhere. Rather than
// compiling two builds or refusing to use a capable machine, the decision is
// made once at startup from the real OS version and every gated feature asks
// here.
//
// The rule for adding to this: a feature belongs behind a gate only when it
// genuinely cannot work on XP. Anything that merely works *better* elsewhere
// should degrade, not disappear.
class OsCapabilities
{
public:
    enum Version {
        Unknown = 0,
        WindowsXp = 51,      // 5.1, and Server 2003 at 5.2
        WindowsVista = 60,
        Windows7 = 61,
        Windows8 = 62,
        Windows81 = 63,
        Windows10OrLater = 100
    };

    static void probe();

    static Version version();
    static QString versionName();
    static bool atLeast(Version required);
    static bool isWindows() ;

    // DXVA2 needs the WDDM display stack, so Vista is the floor. On XP the
    // dxva2.dll it lives in simply is not present, and DXVA 1.0 — which XP
    // does have — is reachable only through DirectShow, which FFmpeg has
    // never supported.
    static bool supportsDxva2();

    // Whether FFmpeg was actually built with the hwaccel, checked separately
    // from the OS because a build can lack it on a machine that has it.
    static bool ffmpegHasDxva2();

    // The combination that matters: hardware decoding is available here.
    static bool hardwareDecodingAvailable();
    static QString hardwareDecodingSummary();

private:
    static Version version_;
    static bool probed_;
    static bool ffmpegDxva2_;
};

#endif
