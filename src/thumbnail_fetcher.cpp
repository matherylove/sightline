#include "thumbnail_fetcher.h"

#include <QDir>
#include <QFile>
#include <QImage>

#include "net_transport.h"

// One worker thread. Several of these share the fetcher's queue.
class ThumbnailWorker : public QThread
{
public:
    explicit ThumbnailWorker(ThumbnailFetcher *owner) : QThread(owner), owner_(owner) {}

protected:
    void run() { owner_->runWorker(); }

private:
    ThumbnailFetcher *owner_;
};

ThumbnailFetcher::ThumbnailFetcher(const QString &cacheDirectory, QObject *parent)
    : QObject(parent), cacheDirectory_(cacheDirectory), stopping_(false)
{
    QDir().mkpath(cacheDirectory_);
}

void ThumbnailFetcher::start()
{
    if (!workers_.isEmpty())
        return;
    // Three: enough that a full grid fills in at once, few enough that the
    // XP TCP stack is not the bottleneck instead.
    for (int i = 0; i < 3; ++i) {
        ThumbnailWorker *worker = new ThumbnailWorker(this);
        workers_.append(worker);
        worker->start(QThread::LowPriority);
    }
}

bool ThumbnailFetcher::takeJob(QString *videoId, QString *url)
{
    QMutexLocker locker(&mutex_);
    while (queue_.isEmpty() && !stopping_)
        wake_.wait(&mutex_);
    if (stopping_)
        return false;

    const QPair<QString, QString> job = queue_.dequeue();
    queued_.removeAll(job.first);
    *videoId = job.first;
    *url = job.second;
    return true;
}

ThumbnailFetcher::~ThumbnailFetcher()
{
    shutdown();
}

void ThumbnailFetcher::request(const QString &videoId, const QString &url)
{
    if (videoId.isEmpty())
        return;

    QMutexLocker locker(&mutex_);
    if (queued_.contains(videoId))
        return;

    // Newest request first: when the user scrolls, what is on screen now
    // matters more than what they scrolled past.
    queued_.append(videoId);
    queue_.prepend(qMakePair(videoId, url));
    locker.unlock();
    wake_.wakeOne();
}

void ThumbnailFetcher::cancelPending()
{
    QMutexLocker locker(&mutex_);
    queue_.clear();
    queued_.clear();
}

void ThumbnailFetcher::shutdown()
{
    QMutexLocker locker(&mutex_);
    stopping_ = true;
    queue_.clear();
    locker.unlock();

    wake_.wakeAll();
    for (int i = 0; i < workers_.size(); ++i) {
        if (workers_.at(i)->isRunning())
            workers_.at(i)->wait(3000);
    }
}

void ThumbnailFetcher::runWorker()
{
    while (true) {
        QString videoId;
        QString jobUrl;
        if (!takeJob(&videoId, &jobUrl))
            return;

        const QString path = cacheDirectory_ + QString::fromLatin1("/")
                           + videoId + QString::fromLatin1(".jpg");

        if (QFile::exists(path)) {
            emit fetched(videoId, path);
            continue;
        }

        // Two candidates. hqdefault always exists; mqdefault is smaller and
        // is tried second only if the first comes back empty.
        QStringList candidates;
        if (!jobUrl.isEmpty())
            candidates << jobUrl;

        // The i.ytimg fallbacks only make sense for a video id; a channel
        // avatar key has no equivalent and would just fetch a 404 twice.
        if (!videoId.startsWith(QLatin1String("ch_"))
            && !videoId.startsWith(QLatin1String("cm_"))) {
            candidates << QString::fromLatin1("https://i.ytimg.com/vi/") + videoId
                            + QString::fromLatin1("/hqdefault.jpg");
            candidates << QString::fromLatin1("https://i.ytimg.com/vi/") + videoId
                            + QString::fromLatin1("/mqdefault.jpg");
        }

        QByteArray payload;
        QString lastError;
        for (int i = 0; i < candidates.size() && payload.isEmpty(); ++i) {
            QMutexLocker check(&mutex_);
            if (stopping_)
                return;
            check.unlock();
            payload = NetTransport::fetch(candidates.at(i), &lastError, 12000);
        }

        if (payload.isEmpty()) {
            emit failed(videoId, lastError);
            continue;
        }

        QImage image;
        if (!image.loadFromData(payload)) {
            emit failed(videoId, QString::fromUtf8("Formato de imagen no reconocido."));
            continue;
        }

        // Stored at grid size, not at source size: a hundred 480x360 images
        // in memory is a real cost on a machine with 512 MB.
        const bool isAvatar = videoId.startsWith(QLatin1String("ch_"))
                           || videoId.startsWith(QLatin1String("cm_"));
        const QSize target = isAvatar ? QSize(56, 56) : QSize(320, 180);
        const QImage scaled = image.scaled(target, Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);
        if (scaled.save(path, "JPG", 82))
            emit fetched(videoId, path);
        else
            emit failed(videoId, QString::fromUtf8("No se pudo guardar la miniatura."));
    }
}
