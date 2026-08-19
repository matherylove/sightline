#include "playback.h"

#include <QTimer>

#include "audio_pump.h"
#include "audio_sink.h"
#include "media_decoder.h"
#include "net_transport.h"
#include "os_capabilities.h"
#include "d3d9_presenter.h"
#include "sync_clock.h"

namespace {
const int kTickMs = 100;
}

PlaybackController::PlaybackController(QObject *parent)
    : QObject(parent),
      state_(Stopped),
      position_(0.0),
      duration_(0.0),
      buffered_(0.0),
      rate_(1.0),
      volume_(80),
      muted_(false),
      droppedFrames_(0),
      networkRate_(0.0),
      undoPosition_(0.0),
      hasUndo_(false),
      tickAccumulator_(0.0),
      clock_(0),
      videoDecoder_(0), audioDecoder_(0), audioSink_(0), audioPump_(0), sync_(0), presenter_(0), surfaceWindow_(0), endedStreams_(0)
{
    clock_ = new QTimer(this);
    clock_->setInterval(kTickMs);
    connect(clock_, SIGNAL(timeout()), this, SLOT(onTick()));
    audioSink_ = new AudioSink(this);

    // Audio never touches the GUI thread again. This is the single change
    // that keeps the clock steady, and the video is paced against that clock.
    audioPump_ = new AudioPump(audioSink_, this);
    audioPump_->start(QThread::TimeCriticalPriority);
    sync_ = new SyncClock;
    presenter_ = new D3D9Presenter;
}

PlaybackController::~PlaybackController()
{
    if (audioPump_)
        audioPump_->stop();
    close();
    delete sync_;
    delete presenter_;
}

void PlaybackController::attachSurface(WId window, const QSize &clientSize)
{
    surfaceWindow_ = window;
    surfaceSize_ = clientSize;
    if (presenter_ && presenter_->isReady())
        presenter_->resize(clientSize);
}

void PlaybackController::detachSurface()
{
    surfaceWindow_ = 0;
    if (presenter_)
        presenter_->shutdown();
}

QString PlaybackController::presenterDescription() const
{
    QString text = presenter_ ? presenter_->describe() : QString();

    // Says which decode path is live, so the difference between an XP box and
    // a Vista one is visible rather than guessed at.
    if (videoDecoder_ && videoDecoder_->usingHardwareDecoding())
        text = QString::fromUtf8("%1 \xC2\xB7 %2").arg(videoDecoder_->hardwareLabel()).arg(text);
    return text;
}

bool PlaybackController::usingGpuPresentation() const
{
    return presenter_ && presenter_->isReady() && presenter_->usingOverlayFormat();
}

void PlaybackController::acknowledgeFrame()
{
    if (videoDecoder_)
        videoDecoder_->acknowledgeFrame();
}

bool PlaybackController::decoding() const
{
    return videoDecoder_ != 0 || audioDecoder_ != 0;
}

void PlaybackController::setTargetSurfaceSize(const QSize &size)
{
    surfaceSize_ = size;
    if (videoDecoder_)
        videoDecoder_->setTargetSize(size);
    if (presenter_ && presenter_->isReady())
        presenter_->resize(size);
}

void PlaybackController::applySettings(const AppSettings &settings)
{
    settings_ = settings;
    volume_ = settings.volume;
}

void PlaybackController::setState(State state)
{
    if (state_ == state)
        return;
    state_ = state;
    emit stateChanged(state_);
}

void PlaybackController::open(const VideoItem &video, const MediaFormat *videoFormat,
                              const MediaFormat *audioFormat, double startAt)
{
    close();

    video_ = video;
    if (videoFormat)
        videoFormat_ = *videoFormat;
    else
        videoFormat_ = MediaFormat();
    if (audioFormat)
        audioFormat_ = *audioFormat;
    else
        audioFormat_ = MediaFormat();

    duration_ = double(video.duration);
    position_ = qBound(0.0, startAt, duration_);
    buffered_ = qMin(duration_, position_ + 6.2);
    droppedFrames_ = 0;

    if (videoFormat_.hasVideo()) {
        videoCodecLabel_ = videoFormat_.videoCodec;
        resolutionLabel_ = QString::fromLatin1("%1x%2")
            .arg(videoFormat_.width).arg(videoFormat_.height);
        networkRate_ = double(videoFormat_.bitrate + audioFormat_.bitrate) / 1000000.0;
    } else if (audioFormat_.hasAudio()) {
        videoCodecLabel_ = audioFormat_.audioCodec;
        resolutionLabel_ = audioFormat_.qualityLabel();
        networkRate_ = double(audioFormat_.bitrate) / 1000000.0;
    } else {
        videoCodecLabel_.clear();
        resolutionLabel_.clear();
        networkRate_ = 0.0;
    }

    setState(Opening);
    emit durationChanged(duration_);
    emit positionChanged(position_);
    emit bufferedChanged(buffered_);
    endedStreams_ = 0;
    sync_->reset(position_);
    sync_->stopFreeRun();

    QString error;

    // Video and audio arrive as two separate files, so two decoders. Audio
    // is opened first because it owns the clock everything else follows.
    if (audioFormat_.hasAudio() && !audioFormat_.url.isEmpty()) {
        audioDecoder_ = new MediaDecoder(MediaDecoder::AudioStream, this);
        connect(audioDecoder_, SIGNAL(endOfStream()), this, SLOT(onDecoderEnded()));
        connect(audioDecoder_, SIGNAL(failed(QString)), this, SLOT(onDecoderFailed(QString)));
        connect(audioDecoder_, SIGNAL(urlExpired()), this, SLOT(onUrlExpired()));

        // FFmpeg opens the URL itself unless Qt is the only stack with TLS
        // here, in which case the custom AVIO bridge takes over.
        const MediaDecoder::Transport transport =
            (NetTransport::kind() == NetTransport::QtSsl && !NetTransport::ffmpegCanDoTls())
                ? MediaDecoder::QtBridgeIo : MediaDecoder::NativeIo;

        bool opened = audioDecoder_->openStream(audioFormat_.url, &error, transport);
        if (!opened && transport == MediaDecoder::NativeIo) {
            // One retry through Qt: a build without a TLS backend fails here
            // and there is no point telling the user before trying the other
            // route we already have.
            opened = audioDecoder_->openStream(audioFormat_.url, &error,
                                               MediaDecoder::QtBridgeIo);
        }
        if (opened) {
            audioDecoder_->setSyncClock(sync_);
            audioSink_->start(audioDecoder_->sampleRate(), audioDecoder_->channelCount());
            audioSink_->setVolume(muted_ ? 0 : volume_);
            audioDecoder_->start();
            audioPump_->setPaused(false);
            audioPump_->attach(audioDecoder_);
        } else {
            delete audioDecoder_;
            audioDecoder_ = 0;
        }
    }

    if (videoFormat_.hasVideo() && !videoFormat_.url.isEmpty()) {
        videoDecoder_ = new MediaDecoder(MediaDecoder::VideoStream, this);
        videoDecoder_->setTargetSize(surfaceSize_);

        // The best each Windows can do: DXVA2 from Vista onwards, software
        // on XP because the display stack it needs does not exist there.
        videoDecoder_->setHardwareDecodingEnabled(OsCapabilities::hardwareDecodingAvailable());
        connect(videoDecoder_, SIGNAL(frameReady(QImage, double)),
                this, SLOT(onFrameReady(QImage, double)));
        connect(videoDecoder_, SIGNAL(openedStream(int, int, double)),
                this, SLOT(onDecoderOpened(int, int, double)));
        connect(videoDecoder_, SIGNAL(endOfStream()), this, SLOT(onDecoderEnded()));
        connect(videoDecoder_, SIGNAL(failed(QString)), this, SLOT(onDecoderFailed(QString)));
        connect(videoDecoder_, SIGNAL(urlExpired()), this, SLOT(onUrlExpired()));

        const MediaDecoder::Transport videoTransport =
            (NetTransport::kind() == NetTransport::QtSsl && !NetTransport::ffmpegCanDoTls())
                ? MediaDecoder::QtBridgeIo : MediaDecoder::NativeIo;

        bool videoOpened = videoDecoder_->openStream(videoFormat_.url, &error, videoTransport);
        if (!videoOpened && videoTransport == MediaDecoder::NativeIo) {
            videoOpened = videoDecoder_->openStream(videoFormat_.url, &error,
                                                    MediaDecoder::QtBridgeIo);
        }
        if (videoOpened) {
            videoDecoder_->setSyncClock(sync_);

            // The device is built once the real frame size is known, so the
            // offscreen surface matches the stream exactly and StretchRect
            // does the resize in one step.
            if (surfaceWindow_ && videoDecoder_->width() > 0) {
                QString presenterError;
                if (presenter_->initialise(surfaceWindow_,
                                           QSize(videoDecoder_->width(), videoDecoder_->height()),
                                           &presenterError)) {
                    presenter_->resize(surfaceSize_);
                    videoDecoder_->setPresenter(presenter_);
                }
            }
            // With no audio track there is nothing to follow, so the clock
            // free-runs from a monotonic timer and the video paces itself
            // against real time instead of decoding flat out.
            if (!audioDecoder_)
                sync_->startFreeRun(position_);
            videoDecoder_->start();
        } else {
            delete videoDecoder_;
            videoDecoder_ = 0;
            emit failed(error);
        }
    }

    if (startAt > 0.5) {
        if (videoDecoder_) videoDecoder_->requestSeek(startAt);
        if (audioDecoder_) audioDecoder_->requestSeek(startAt);
    }

    play();
}

void PlaybackController::close()
{
    clock_->stop();

    if (videoDecoder_) {
        videoDecoder_->setPresenter(0);
        videoDecoder_->stop();
        delete videoDecoder_;
        videoDecoder_ = 0;
    }
    if (presenter_)
        presenter_->shutdown();
    if (audioPump_)
        audioPump_->detach();
    if (audioDecoder_) {
        audioDecoder_->stop();
        delete audioDecoder_;
        audioDecoder_ = 0;
    }
    if (audioSink_)
        audioSink_->stop();
    if (sync_) {
        sync_->stopFreeRun();
        sync_->reset(0.0);
    }
    endedStreams_ = 0;

    segments_.clear();
    hasUndo_ = false;
    tickAccumulator_ = 0.0;
    position_ = 0.0;
    duration_ = 0.0;
    buffered_ = 0.0;
    setState(Stopped);
}

void PlaybackController::play()
{
    if (video_.id.isEmpty())
        return;
    clock_->start();
    sync_->setPaused(false);
    if (videoDecoder_) videoDecoder_->setPaused(false);
    if (audioDecoder_) audioDecoder_->setPaused(false);
    if (audioSink_) audioSink_->setPaused(false);
    if (audioPump_) audioPump_->setPaused(false);
    setState(Playing);
}

void PlaybackController::pause()
{
    clock_->stop();
    sync_->setPaused(true);
    if (videoDecoder_) videoDecoder_->setPaused(true);
    if (audioDecoder_) audioDecoder_->setPaused(true);
    if (audioSink_) audioSink_->setPaused(true);
    if (audioPump_) audioPump_->setPaused(true);
    if (state_ == Playing)
        setState(Paused);
}

void PlaybackController::togglePause()
{
    if (state_ == Playing)
        pause();
    else
        play();
}

void PlaybackController::seek(double seconds)
{
    position_ = qBound(0.0, seconds, duration_);

    // Buffered never goes backwards past the new position: seeking forward
    // into unbuffered territory has to look like it, or the bar lies.
    if (buffered_ < position_)
        buffered_ = position_;

    // Seeking by hand clears the undo, because the position it would return
    // to is no longer where the user was.
    hasUndo_ = false;

    sync_->reset(position_);
    if (videoDecoder_) videoDecoder_->requestSeek(position_);
    if (audioDecoder_) audioDecoder_->requestSeek(position_);

    emit positionChanged(position_);
    emit bufferedChanged(buffered_);
}

void PlaybackController::step(double deltaSeconds)
{
    seek(position_ + deltaSeconds);
}

void PlaybackController::setRate(double rate)
{
    rate_ = qBound(0.25, rate, 4.0);
}

void PlaybackController::setVolume(int volume)
{
    volume_ = qBound(0, volume, 100);
    if (audioSink_)
        audioSink_->setVolume(muted_ ? 0 : volume_);
}

void PlaybackController::setMuted(bool muted)
{
    muted_ = muted;
    if (audioSink_)
        audioSink_->setVolume(muted_ ? 0 : volume_);
}

void PlaybackController::setSegments(const QList<SponsorSegment> &segments)
{
    segments_ = segments;
}

void PlaybackController::undoLastSkip()
{
    if (!hasUndo_)
        return;
    hasUndo_ = false;
    position_ = undoPosition_;
    emit positionChanged(position_);
}

void PlaybackController::onTick()
{
    if (state_ != Playing)
        return;

    double delta = (kTickMs / 1000.0) * rate_;

    // With a decoder attached the position comes from the audio clock minus
    // what is still sitting in the sound buffer, which is what the listener
    // is actually hearing. Wall time is only the fallback for a stream with
    // no audio track at all.
    if (audioDecoder_) {
        // What the listener is actually hearing: the decoded position minus
        // whatever is still sitting in the DirectSound ring.
        const double heard = audioDecoder_->clock() - audioSink_->latencySeconds();
        if (heard > 0.0) {
            delta = heard - position_;
            position_ = heard;
            sync_->set(heard);
        } else {
            position_ += delta;
        }
    } else if (videoDecoder_) {
        const double shown = sync_->get();
        if (shown > 0.0) {
            delta = shown - position_;
            position_ = shown;
        } else {
            position_ += delta;
        }
    } else {
        position_ += delta;
    }

    if (delta < 0.0)
        delta = 0.0;
    tickAccumulator_ += delta;

    // The listening log counts whole seconds of actual playback, not wall
    // clock time, so pausing does not inflate anyone's statistics.
    while (tickAccumulator_ >= 1.0) {
        tickAccumulator_ -= 1.0;
        emit secondPlayed();
    }

    if (audioDecoder_ || videoDecoder_) {
        const MediaDecoder *lead = audioDecoder_ ? audioDecoder_ : videoDecoder_;
        Q_UNUSED(lead);
        buffered_ = qMin(duration_, position_ + 6.0);
    } else if (buffered_ < duration_) {
        buffered_ = qMin(duration_, buffered_ + delta * 1.4);
    }

    if (duration_ > 0.0 && position_ >= duration_) {
        position_ = duration_;
        clock_->stop();
        emit positionChanged(position_);
        setState(Ended);
        emit finished();
        return;
    }

    checkSegments();
    emit positionChanged(position_);
    emit bufferedChanged(buffered_);
}

void PlaybackController::checkSegments()
{
    if (!settings_.sponsorBlockEnabled || segments_.isEmpty())
        return;

    for (int i = 0; i < segments_.size(); ++i) {
        SponsorSegment &segment = segments_[i];
        if (segment.skipped)
            continue;
        if (position_ < segment.start || position_ >= segment.end)
            continue;

        const SegmentAction action = settings_.actionFor(segment.category);
        if (action == SegmentIgnore || action == SegmentMarkOnly)
            continue;

        if (action == SegmentPrompt) {
            emit segmentPending(segment.category, segment.start, segment.end);
            segment.skipped = true;   // asked once; not asked again this run
            continue;
        }

        // Skip silently, but never without saying so: the toast goes up and
        // stays undoable for five seconds.
        undoPosition_ = position_;
        hasUndo_ = true;
        const double saved = segment.end - position_;
        segment.skipped = true;
        position_ = segment.end;
        if (buffered_ < position_)
            buffered_ = position_;
        emit segmentSkipped(segment.category, saved);
        return;
    }
}


void PlaybackController::onFrameReady(const QImage &frame, double presentationTime)
{
    Q_UNUSED(presentationTime);
    emit frameReady(frame);
}

void PlaybackController::onDecoderOpened(int width, int height, double duration)
{
    if (width > 0 && height > 0) {
        resolutionLabel_ = QString::fromLatin1("%1x%2").arg(width).arg(height);
        if (videoDecoder_)
            videoCodecLabel_ = videoDecoder_->codecName();
    }

    // The container's own duration wins over the one the metadata claimed:
    // a flat listing sometimes rounds, and the seek bar has to be exact.
    if (duration > 1.0 && qAbs(duration - duration_) > 1.0) {
        duration_ = duration;
        emit durationChanged(duration_);
    }
}

void PlaybackController::onDecoderFailed(const QString &message)
{
    setState(Failed);
    emit failed(message);
}

void PlaybackController::onDecoderEnded()
{
    // Two streams have to finish before the video is over; ending on the
    // first would cut the last seconds of whichever ran shorter.
    const int expected = (videoDecoder_ ? 1 : 0) + (audioDecoder_ ? 1 : 0);
    if (++endedStreams_ < expected)
        return;

    clock_->stop();
    setState(Ended);
    emit finished();
}

void PlaybackController::onUrlExpired()
{
    pause();
    emit urlExpired();
}

