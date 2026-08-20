#include "lyrics_service.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QThread>
#include <QUrl>
#include <QWaitCondition>

#include "net_transport.h"

// The lookup blocks, so it lives on a worker thread. One at a time is plenty:
// only the track that is playing needs lyrics.
class LyricsWorker : public QThread
{
public:
    explicit LyricsWorker(QObject *parent = 0)
        : QThread(parent), stopping_(false) {}

    void enqueue(const QString &videoId, const QString &url)
    {
        QMutexLocker locker(&mutex_);
        pendingId_ = videoId;
        pendingUrl_ = url;
        locker.unlock();
        wake_.wakeOne();
    }

    void stop()
    {
        QMutexLocker locker(&mutex_);
        stopping_ = true;
        locker.unlock();
        wake_.wakeAll();
        if (isRunning())
            wait(3000);
    }

    QString takeResult(QString *videoId)
    {
        QMutexLocker locker(&mutex_);
        *videoId = resultId_;
        const QString payload = result_;
        result_.clear();
        resultId_.clear();
        return payload;
    }

protected:
    void run()
    {
        while (true) {
            QMutexLocker locker(&mutex_);
            while (pendingId_.isEmpty() && !stopping_)
                wake_.wait(&mutex_);
            if (stopping_)
                return;

            const QString videoId = pendingId_;
            const QString url = pendingUrl_;
            pendingId_.clear();
            pendingUrl_.clear();
            locker.unlock();

            QString error;
            const QByteArray payload = NetTransport::fetch(url, &error, 10000);

            QMutexLocker store(&mutex_);
            resultId_ = videoId;
            result_ = QString::fromUtf8(payload);
            store.unlock();

            QMetaObject::invokeMethod(parent(), "onFetched", Qt::QueuedConnection);
        }
    }

private:
    QMutex mutex_;
    QWaitCondition wake_;
    QString pendingId_;
    QString pendingUrl_;
    QString resultId_;
    QString result_;
    bool stopping_;
};

LyricsService::LyricsService(const SightlinePaths &paths, QObject *parent)
    : QObject(parent), paths_(paths), worker_(0)
{
    worker_ = new LyricsWorker(this);
    worker_->start(QThread::LowPriority);
}

LyricsService::~LyricsService()
{
    if (worker_)
        worker_->stop();
}

QString LyricsService::cacheFile(const QString &videoId) const
{
    return paths_.lyrics() + QString::fromLatin1("/") + videoId + QString::fromLatin1(".lrc");
}

bool LyricsService::loadFromCache(const QString &videoId, QList<LyricLine> *lines,
                                  bool *synced) const
{
    QFile file(cacheFile(videoId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString text = QString::fromUtf8(file.readAll());
    if (text.isEmpty())
        return false;
    *lines = parseLrc(text, synced);
    return !lines->isEmpty();
}

void LyricsService::request(const VideoItem &video)
{
    if (video.id.isEmpty())
        return;

    QList<LyricLine> cached;
    bool synced = false;
    if (loadFromCache(video.id, &cached, &synced)) {
        emit lyricsReady(video.id, cached, synced);
        return;
    }

    const QString title = cleanTitle(video.title);
    const QString artist = cleanArtist(video.channelName);
    if (title.isEmpty()) {
        emit lyricsMissing(video.id);
        return;
    }

    QString url = QString::fromLatin1("https://lrclib.net/api/get?track_name=")
        + QString::fromUtf8(QUrl::toPercentEncoding(title))
        + QString::fromLatin1("&artist_name=")
        + QString::fromUtf8(QUrl::toPercentEncoding(artist));
    if (video.duration > 0)
        url += QString::fromLatin1("&duration=") + QString::number(video.duration);

    worker_->enqueue(video.id, url);
}

void LyricsService::cancel()
{
}

void LyricsService::onFetched()
{
    QString videoId;
    const QString payload = worker_->takeResult(&videoId);
    if (videoId.isEmpty())
        return;

    if (payload.isEmpty()) {
        emit lyricsMissing(videoId);
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(payload.toUtf8()).object();
    QString text = root.value(QString::fromLatin1("syncedLyrics")).toString();
    bool wasSynced = !text.isEmpty();
    if (text.isEmpty())
        text = root.value(QString::fromLatin1("plainLyrics")).toString();

    if (text.isEmpty()) {
        emit lyricsMissing(videoId);
        return;
    }

    // Written out as .lrc so the file is useful outside Sightline too.
    QFile file(cacheFile(videoId));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(text.toUtf8());

    bool synced = false;
    const QList<LyricLine> lines = parseLrc(text, &synced);
    Q_UNUSED(wasSynced);

    if (lines.isEmpty())
        emit lyricsMissing(videoId);
    else
        emit lyricsReady(videoId, lines, synced);
}
