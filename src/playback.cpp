#include "playback.h"

#include <QTimer>

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
      clock_(0)
{
    clock_ = new QTimer(this);
    clock_->setInterval(kTickMs);
    connect(clock_, SIGNAL(timeout()), this, SLOT(onTick()));
}

PlaybackController::~PlaybackController()
{
    close();
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

    play();
}

void PlaybackController::close()
{
    clock_->stop();
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
    setState(Playing);
}

void PlaybackController::pause()
{
    clock_->stop();
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
}

void PlaybackController::setMuted(bool muted)
{
    muted_ = muted;
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

    const double delta = (kTickMs / 1000.0) * rate_;
    position_ += delta;
    tickAccumulator_ += delta;

    // The listening log counts whole seconds of actual playback, not wall
    // clock time, so pausing does not inflate anyone's statistics.
    while (tickAccumulator_ >= 1.0) {
        tickAccumulator_ -= 1.0;
        emit secondPlayed();
    }

    if (buffered_ < duration_)
        buffered_ = qMin(duration_, buffered_ + delta * 1.4);

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
