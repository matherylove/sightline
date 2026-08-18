#ifndef SIGHTLINE_PLAYBACK_H
#define SIGHTLINE_PLAYBACK_H

#include <QImage>
#include <QList>
#include <QObject>
#include <QSize>
#include <QtGui/qwindowdefs.h>
#include <QString>

#include "app_settings.h"
#include "media_types.h"

class QTimer;
class MediaDecoder;
class AudioSink;
class SyncClock;
class D3D9Presenter;

// The playback clock and the SponsorBlock skip logic.
//
// The decoder is deliberately behind this boundary. Everything above it —
// the transport, the seek bar, the statistics, the skip prompts — is driven
// by position() and duration(), so wiring FFmpeg in later means implementing
// one interface rather than touching the interface.
//
// Build with CONFIG+=ffmpeg to compile the libav* backend; without it the
// controller still runs the clock, which is what drives every widget.
class PlaybackController : public QObject
{
    Q_OBJECT

public:
    enum State { Stopped, Opening, Playing, Paused, Ended, Failed };

    explicit PlaybackController(QObject *parent = 0);
    ~PlaybackController();

    void applySettings(const AppSettings &settings);

    void open(const VideoItem &video, const MediaFormat *videoFormat,
              const MediaFormat *audioFormat, double startAt = 0.0);
    void close();

public slots:
    void play();
    void pause();
    void togglePause();
    void seek(double seconds);
    void step(double deltaSeconds);
    void undoLastSkip();

public:

    State state() const { return state_; }
    bool isPlaying() const { return state_ == Playing; }
    double position() const { return position_; }
    double duration() const { return duration_; }
    double buffered() const { return buffered_; }
    double rate() const { return rate_; }
    void setRate(double rate);

    int volume() const { return volume_; }
    void setVolume(int volume);
    bool muted() const { return muted_; }
    void setMuted(bool muted);

    VideoItem video() const { return video_; }
    QString videoCodecLabel() const { return videoCodecLabel_; }
    QString resolutionLabel() const { return resolutionLabel_; }
    int droppedFrames() const { return droppedFrames_; }
    double networkRate() const { return networkRate_; }

    void setTargetSurfaceSize(const QSize &size);

    // Called by the view once a frame has been painted.
    void acknowledgeFrame();

    // Attaches the video canvas so frames can be blitted straight to it.
    void attachSurface(WId window, const QSize &clientSize);
    void detachSurface();
    QString presenterDescription() const;
    bool usingGpuPresentation() const;
    bool decoding() const;

    void setSegments(const QList<SponsorSegment> &segments);
    QList<SponsorSegment> segments() const { return segments_; }

signals:
    void urlExpired();
    void stateChanged(PlaybackController::State state);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void bufferedChanged(double seconds);
    void frameReady(const QImage &frame);
    void finished();
    void failed(const QString &message);

    // Emitted when a segment was jumped over on its own, so the surface can
    // put up the undo toast. skippedSeconds is what was actually saved.
    void segmentSkipped(SponsorSegment::Category category, double skippedSeconds);
    // Emitted for categories set to "prompt": the UI offers a button.
    void segmentPending(SponsorSegment::Category category, double start, double end);
    void secondPlayed();

private slots:
    void onTick();
    void onFrameReady(const QImage &frame, double presentationTime);
    void onDecoderOpened(int width, int height, double duration);
    void onDecoderFailed(const QString &message);
    void onDecoderEnded();
    void onUrlExpired();
    void onAudioReady();

private:
    void setState(State state);
    void checkSegments();

    AppSettings settings_;
    VideoItem video_;
    MediaFormat videoFormat_;
    MediaFormat audioFormat_;

    State state_;
    double position_;
    double duration_;
    double buffered_;
    double rate_;
    int volume_;
    bool muted_;

    QString videoCodecLabel_;
    QString resolutionLabel_;
    int droppedFrames_;
    double networkRate_;

    QList<SponsorSegment> segments_;
    double undoPosition_;
    bool hasUndo_;
    double tickAccumulator_;

    QTimer *clock_;

    MediaDecoder *videoDecoder_;
    MediaDecoder *audioDecoder_;
    AudioSink *audioSink_;
    SyncClock *sync_;
    D3D9Presenter *presenter_;
    WId surfaceWindow_;
    QSize surfaceSize_;
    int endedStreams_;
};

#endif
