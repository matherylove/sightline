#ifndef SIGHTLINE_THUMBNAIL_FETCHER_H
#define SIGHTLINE_THUMBNAIL_FETCHER_H

#include <QMutex>
#include <QQueue>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

// Downloads thumbnails on a worker thread, through whichever transport this
// machine actually has.
//
// The grid asks for twenty-odd images at once, so this cannot be one
// blocking call per card on the GUI thread. It also cannot be
// QNetworkAccessManager alone: on a Qt built without OpenSSL every https
// request fails, and the whole interface ends up as grey placeholder tiles
// with no explanation.
//
// hqdefault.jpg is requested rather than the WebP variants YouTube prefers
// now, because Qt 5.6 ships no WebP image plugin and a static build will
// not gain one.
class ThumbnailFetcher : public QThread
{
    Q_OBJECT

public:
    explicit ThumbnailFetcher(const QString &cacheDirectory, QObject *parent = 0);
    ~ThumbnailFetcher();

    void request(const QString &videoId, const QString &url);
    void cancelPending();
    void shutdown();

signals:
    void fetched(const QString &videoId, const QString &filePath);
    void failed(const QString &videoId, const QString &message);

protected:
    void run();

private:
    QString cacheDirectory_;
    QQueue<QPair<QString, QString> > queue_;
    QStringList queued_;
    mutable QMutex mutex_;
    QWaitCondition wake_;
    bool stopping_;
};

#endif
