#include "net_transport.h"

#include <QSslSocket>

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

NetTransport::Kind NetTransport::kind_ = NetTransport::None;
bool NetTransport::probed_ = false;
bool NetTransport::ffmpegTls_ = false;

bool NetTransport::qtCanDoTls()
{
    return QSslSocket::supportsSsl();
}

bool NetTransport::ffmpegCanDoTls()
{
    // avio_find_protocol_name only tells us the protocol is registered, not
    // that a handshake would succeed, but a build without TLS does not
    // register https at all, so this is the cheap and correct check.
    const char *name = avio_find_protocol_name("https://example.invalid/x");
    return name != 0 && QString::fromLatin1(name) == QLatin1String("https");
}

void NetTransport::probe()
{
    if (probed_)
        return;
    probed_ = true;

    avformat_network_init();
    ffmpegTls_ = ffmpegCanDoTls();

    if (qtCanDoTls())
        kind_ = QtSsl;
    else if (ffmpegTls_)
        kind_ = FfmpegAvio;
    else
        kind_ = None;
}

NetTransport::Kind NetTransport::kind()
{
    probe();
    return kind_;
}

QString NetTransport::sslLibraryVersion()
{
    const QString runtime = QSslSocket::sslLibraryVersionString();
    return runtime.isEmpty() ? QString::fromUtf8("sin OpenSSL") : runtime;
}

QString NetTransport::description()
{
    switch (kind()) {
    case QtSsl:
        return QString::fromUtf8("TLS por Qt (%1)").arg(sslLibraryVersion());
    case FfmpegAvio:
        return QString::fromUtf8("TLS por FFmpeg (Qt sin OpenSSL)");
    default:
        break;
    }
    return QString::fromUtf8("sin TLS: solo yt-dlp llega a la red");
}

QByteArray NetTransport::fetch(const QString &url, QString *error, int timeoutMs)
{
    probe();

    AVDictionary *options = 0;
    av_dict_set(&options, "user_agent",
        "Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Sightline/0.1", 0);
    av_dict_set_int(&options, "rw_timeout", qint64(timeoutMs) * 1000, 0);
    av_dict_set_int(&options, "timeout", qint64(timeoutMs) * 1000, 0);
    av_dict_set(&options, "reconnect", "1", 0);

    // Without a whitelist FFmpeg would follow a redirect into file:// or
    // anything else it supports, which is not something a thumbnail URL
    // should be able to ask for.
    av_dict_set(&options, "protocol_whitelist", "http,https,tcp,tls", 0);

    AVIOContext *context = 0;
    const int opened = avio_open2(&context, url.toUtf8().constData(),
                                  AVIO_FLAG_READ, 0, &options);
    av_dict_free(&options);

    if (opened < 0) {
        if (error) {
            char text[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(opened, text, sizeof(text));
            *error = QString::fromUtf8(text);
        }
        return QByteArray();
    }

    QByteArray payload;
    QByteArray chunk;
    chunk.resize(32 * 1024);

    while (true) {
        const int read = avio_read(context, reinterpret_cast<unsigned char *>(chunk.data()),
                                   chunk.size());
        if (read <= 0)
            break;
        payload.append(chunk.constData(), read);

        // A thumbnail that is not a thumbnail: stop rather than paging in
        // whatever a misdirected URL is actually serving.
        if (payload.size() > 8 * 1024 * 1024)
            break;
    }

    avio_closep(&context);

    if (payload.isEmpty() && error)
        *error = QString::fromUtf8("Respuesta vacía.");
    return payload;
}
