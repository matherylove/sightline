#include "media_source.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>

extern "C" {
#include <libavformat/avio.h>
}

namespace {
// 8 MB of slack. Enough that a 1080p H.264 stream at 4 Mb/s keeps roughly
// fifteen seconds ahead of the decoder without the window itself becoming a
// memory problem on a machine with 512 MB.
const int kWindowLimit = 8 * 1024 * 1024;
const int kRefillThreshold = 1 * 1024 * 1024;
}

MediaSource::MediaSource(QObject *parent)
    : QObject(parent),
      network_(0), reply_(0),
      windowStart_(0), readPosition_(0), size_(0),
      stopping_(false), finished_(false), expired_(false), requestInFlight_(false)
{
    network_ = new QNetworkAccessManager(this);
    connect(this, SIGNAL(requestRange(qint64)), this, SLOT(onRequestRange(qint64)),
            Qt::QueuedConnection);
}

MediaSource::~MediaSource()
{
    close();
}

void MediaSource::open(const QString &url, qint64 knownSize)
{
    close();

    QMutexLocker locker(&mutex_);
    url_ = url;
    size_ = knownSize;
    windowStart_ = 0;
    readPosition_ = 0;
    window_.clear();
    stopping_ = false;
    finished_ = false;
    expired_ = false;
    requestInFlight_ = false;
    lastError_.clear();
    locker.unlock();

    emit requestRange(0);
}

void MediaSource::close()
{
    QMutexLocker locker(&mutex_);
    stopping_ = true;
    locker.unlock();

    // Wake anything blocked in readAt so the decoder thread can unwind
    // instead of sitting on the condition forever.
    dataReady_.wakeAll();

    if (QThread::currentThread() == thread())
        abortReply();
}

void MediaSource::abortReply()
{
    if (!reply_)
        return;
    reply_->disconnect(this);
    reply_->abort();
    reply_->deleteLater();
    reply_ = 0;
    requestInFlight_ = false;
}

void MediaSource::onRequestRange(qint64 offset)
{
    abortReply();

    QMutexLocker locker(&mutex_);
    if (stopping_ || url_.isEmpty())
        return;
    const QString url = url_;
    windowStart_ = offset;
    window_.clear();
    finished_ = false;
    requestInFlight_ = true;
    locker.unlock();

    QNetworkRequest request((QUrl(url)));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Sightline/0.1");
    if (offset > 0) {
        request.setRawHeader("Range",
            QByteArray("bytes=") + QByteArray::number(offset) + "-");
    }

    reply_ = network_->get(request);
    connect(reply_, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
    connect(reply_, SIGNAL(finished()), this, SLOT(onReplyFinished()));
}

void MediaSource::onReadyRead()
{
    if (!reply_)
        return;

    const QByteArray chunk = reply_->readAll();
    if (chunk.isEmpty())
        return;

    QMutexLocker locker(&mutex_);

    if (size_ == 0) {
        // Content-Range carries the true total on a partial response;
        // Content-Length alone would only describe the remaining tail.
        const QByteArray contentRange = reply_->rawHeader("Content-Range");
        const int slash = contentRange.lastIndexOf('/');
        if (slash >= 0)
            size_ = contentRange.mid(slash + 1).toLongLong();
        else
            size_ = reply_->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    }

    window_.append(chunk);

    // Drop what the decoder has already consumed, but only once the window
    // has grown past the limit, so short backward seeks still hit memory.
    if (window_.size() > kWindowLimit) {
        const qint64 consumed = readPosition_ - windowStart_;
        if (consumed > kRefillThreshold) {
            window_.remove(0, int(consumed));
            windowStart_ = readPosition_;
        }
    }

    locker.unlock();
    dataReady_.wakeAll();
}

void MediaSource::onReplyFinished()
{
    if (!reply_)
        return;

    const int status = reply_->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply_->error();

    QMutexLocker locker(&mutex_);
    requestInFlight_ = false;

    // 403 is what googlevideo answers once the URL's roughly six hour life
    // is up, or when it is replayed from a different address. It is not a
    // transport failure and must not be reported as one.
    if (status == 403 || status == 410) {
        expired_ = true;
        lastError_ = QString::fromUtf8("La URL del stream caducó (HTTP %1).").arg(status);
        locker.unlock();
        dataReady_.wakeAll();
        emit urlExpired();
        return;
    }

    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        lastError_ = reply_->errorString();
        finished_ = true;
        locker.unlock();
        dataReady_.wakeAll();
        emit errorOccurred(lastError_);
        return;
    }

    finished_ = true;
    locker.unlock();
    dataReady_.wakeAll();
}

qint64 MediaSource::bufferedBytes() const
{
    QMutexLocker locker(&mutex_);
    const qint64 available = windowStart_ + window_.size() - readPosition_;
    return available > 0 ? available : 0;
}

int MediaSource::readAt(unsigned char *buffer, int size)
{
    QMutexLocker locker(&mutex_);

    while (true) {
        if (stopping_ || expired_)
            return AVERROR_EOF;

        const qint64 windowEnd = windowStart_ + window_.size();

        if (readPosition_ >= windowStart_ && readPosition_ < windowEnd) {
            const int offset = int(readPosition_ - windowStart_);
            const int available = window_.size() - offset;
            const int count = qMin(size, available);
            memcpy(buffer, window_.constData() + offset, count);
            readPosition_ += count;
            return count;
        }

        if (size_ > 0 && readPosition_ >= size_)
            return AVERROR_EOF;

        if (readPosition_ < windowStart_ || readPosition_ > windowEnd) {
            // Seeked outside the window: ask the network thread for a new
            // range and wait for it rather than failing the read.
            if (!requestInFlight_) {
                requestInFlight_ = true;
                const qint64 target = readPosition_;
                locker.unlock();
                emit requestRange(target);
                locker.relock();
            }
        } else if (finished_) {
            return AVERROR_EOF;
        }

        // Five seconds: long enough to ride out a stall on a slow line,
        // short enough that a dead connection does not hang the decoder.
        if (!dataReady_.wait(&mutex_, 5000)) {
            if (finished_ || stopping_)
                return AVERROR_EOF;
            return AVERROR(EAGAIN);
        }
    }
}

qint64 MediaSource::seekTo(qint64 offset, int whence)
{
    QMutexLocker locker(&mutex_);

    if (whence == AVSEEK_SIZE)
        return size_ > 0 ? size_ : -1;

    qint64 target = offset;
    if (whence == SEEK_CUR)
        target = readPosition_ + offset;
    else if (whence == SEEK_END)
        target = (size_ > 0 ? size_ : 0) + offset;

    if (target < 0)
        return -1;
    if (size_ > 0 && target > size_)
        target = size_;

    readPosition_ = target;

    const qint64 windowEnd = windowStart_ + window_.size();
    if (target < windowStart_ || target > windowEnd) {
        if (!requestInFlight_) {
            requestInFlight_ = true;
            locker.unlock();
            emit requestRange(target);
            locker.relock();
        }
    }
    return target;
}
