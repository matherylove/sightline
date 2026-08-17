#ifndef SIGHTLINE_MEDIA_SOURCE_H
#define SIGHTLINE_MEDIA_SOURCE_H

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QWaitCondition>

class QNetworkAccessManager;
class QNetworkReply;

// Feeds FFmpeg from Qt's network stack instead of letting libavformat open
// the URL itself.
//
// This is not a detour, it is the only workable route on XP. FFmpeg's own
// HTTPS would go through whatever TLS backend it was built against; on XP
// that is Schannel, which tops out at TLS 1.0, and googlevideo has required
// 1.2 for years. Qt is already linked against an OpenSSL we control, so the
// bytes come in through Qt and FFmpeg sees a plain custom AVIOContext.
//
// It also means one place owns the 403-on-expiry behaviour: when a URL dies
// mid-playback the source reports it and the player re-extracts, rather than
// libavformat returning a generic I/O error from inside a decode call.
class MediaSource : public QObject
{
    Q_OBJECT

public:
    explicit MediaSource(QObject *parent = 0);
    ~MediaSource();

    // Must be called from the thread that will own the network requests.
    void open(const QString &url, qint64 knownSize = 0);
    void close();

    // Called by the decoder thread through the AVIOContext callbacks. Both
    // block until data is available, the stream ends, or close() is called.
    int readAt(unsigned char *buffer, int size);
    qint64 seekTo(qint64 offset, int whence);

    qint64 size() const { return size_; }
    qint64 bufferedBytes() const;
    bool expired() const { return expired_; }
    bool finished() const { return finished_; }
    QString lastError() const { return lastError_; }

signals:
    // Queued to the network thread; the decoder thread never touches QNAM.
    void requestRange(qint64 offset);
    void urlExpired();
    void errorOccurred(const QString &message);

private slots:
    void onRequestRange(qint64 offset);
    void onReadyRead();
    void onReplyFinished();

private:
    void abortReply();

    QNetworkAccessManager *network_;
    QNetworkReply *reply_;
    QString url_;

    mutable QMutex mutex_;
    QWaitCondition dataReady_;

    // A single sliding window rather than a full cache: seeking backwards
    // beyond it re-requests, which is cheaper than holding a 4 GB file in
    // 2 GB of user address space.
    QByteArray window_;
    qint64 windowStart_;
    qint64 readPosition_;
    qint64 size_;

    bool stopping_;
    bool finished_;
    bool expired_;
    bool requestInFlight_;
    QString lastError_;
};

#endif
