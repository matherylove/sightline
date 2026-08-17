#ifndef SIGHTLINE_NET_TRANSPORT_H
#define SIGHTLINE_NET_TRANSPORT_H

#include <QByteArray>
#include <QString>

// Which stack can actually reach an https:// URL on this machine.
//
// The static Qt 5.6 builds that still target XP are usually linked without
// OpenSSL, and Qt has no Schannel backend before 5.14. The result is that
// every QNetworkAccessManager request to https:// fails with a bare
// "unknown error" and no TLS handshake ever happens. That takes out
// thumbnails, googlevideo, SponsorBlock and OAuth in one go, which is why
// this is decided once at startup and reported in the status bar rather
// than being rediscovered as four separate mysteries.
//
// FFmpeg's avio is the fallback: this XP build carries its own TLS, and it
// has to work anyway or no video would play.
class NetTransport
{
public:
    enum Kind {
        QtSsl,        // Qt has OpenSSL; use QNetworkAccessManager
        FfmpegAvio,   // Qt cannot do TLS; borrow FFmpeg's protocol layer
        None          // neither works; only yt-dlp can reach the network
    };

    static void probe();
    static Kind kind();
    static bool qtCanDoTls();
    static bool ffmpegCanDoTls();
    static QString description();
    static QString sslLibraryVersion();

    // Blocking fetch through FFmpeg's avio. Used for small resources such as
    // thumbnails and API responses when Qt cannot do TLS. Returns an empty
    // array on failure and fills error when given.
    static QByteArray fetch(const QString &url, QString *error = 0, int timeoutMs = 15000);

private:
    static Kind kind_;
    static bool probed_;
    static bool ffmpegTls_;
};

#endif
