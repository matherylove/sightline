#ifndef SIGHTLINE_THUMBNAIL_FETCHER_H
#define SIGHTLINE_THUMBNAIL_FETCHER_H

#include <QMutex>
#include <QQueue>
#include <QStringList>
#include <QList>
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
class ThumbnailWorker;

// The queue is shared by a small pool of workers. One at a time made a grid
// of twenty-four cards fill in visibly one by one over several seconds; three
// concurrent fetches make it feel immediate without opening enough sockets to
// matter on an XP-era stack.
class ThumbnailFetcher : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailFetcher(const QString &cacheDirectory, QObject *parent = 0);
    ~ThumbnailFetcher();

    void request(const QString &videoId, const QString &url);
    void cancelPending();
    void shutdown();
    void start();

signals:
    void fetched(const QString &videoId, const QString &filePath);
    void failed(const QString &videoId, const QString &message);

private:
    friend class ThumbnailWorker;
    bool takeJob(QString *videoId, QString *url);
    void runWorker();

    QList<ThumbnailWorker *> workers_;
    QString cacheDirectory_;
    QQueue<QPair<QString, QString> > queue_;
    QStringList queued_;
    mutable QMutex mutex_;
    QWaitCondition wake_;
    bool stopping_;
};

#endif
