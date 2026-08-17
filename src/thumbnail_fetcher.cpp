#include "thumbnail_fetcher.h"

#include <QDir>
#include <QFile>
#include <QImage>

#include "net_transport.h"

ThumbnailFetcher::ThumbnailFetcher(const QString &cacheDirectory, QObject *parent)
    : QThread(parent), cacheDirectory_(cacheDirectory), stopping_(false)
{
    QDir().mkpath(cacheDirectory_);
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
    if (isRunning())
        wait(3000);
}

void ThumbnailFetcher::run()
{
    while (true) {
        QMutexLocker locker(&mutex_);
        while (queue_.isEmpty() && !stopping_)
            wake_.wait(&mutex_);

        if (stopping_)
            return;

        const QPair<QString, QString> job = queue_.dequeue();
        queued_.removeAll(job.first);
        locker.unlock();

        const QString videoId = job.first;
        const QString path = cacheDirectory_ + QString::fromLatin1("/")
                           + videoId + QString::fromLatin1(".jpg");

        if (QFile::exists(path)) {
            emit fetched(videoId, path);
            continue;
        }

        // Two candidates. hqdefault always exists; mqdefault is smaller and
        // is tried second only if the first comes back empty.
        QStringList candidates;
        if (!job.second.isEmpty() && job.second.endsWith(QLatin1String(".jpg")))
            candidates << job.second;
        candidates << QString::fromLatin1("https://i.ytimg.com/vi/") + videoId
                        + QString::fromLatin1("/hqdefault.jpg");
        candidates << QString::fromLatin1("https://i.ytimg.com/vi/") + videoId
                        + QString::fromLatin1("/mqdefault.jpg");

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
        const QImage scaled = image.scaled(QSize(320, 180), Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);
        if (scaled.save(path, "JPG", 82))
            emit fetched(videoId, path);
        else
            emit failed(videoId, QString::fromUtf8("No se pudo guardar la miniatura."));
    }
}
