#ifndef SIGHTLINE_MEDIA_DECODER_H
#define SIGHTLINE_MEDIA_DECODER_H

#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QString>
#include <QAtomicInt>
#include <QThread>
#include <QWaitCondition>

struct AVCodecContext;
struct AVFormatContext;
struct AVIOContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;

class MediaSource;
class SyncClock;
class D3D9Presenter;

// Demuxes and decodes one adaptive stream on its own thread.
//
// YouTube serves video and audio as separate files, so the player runs two
// of these: one for the picture, one for the sound. Each owns its own
// AVFormatContext over a MediaSource, and the audio one drives the clock.
//
// The scaler converts straight to RGB32 rather than handing out YUV, because
// on XP there is no DXVA2 and no GPU colour conversion worth relying on; a
// QImage the surface can blit is the honest output here. When the D3D9 path
// lands it will take the YUV planes instead and this stays as the fallback.
class MediaDecoder : public QThread
{
    Q_OBJECT

public:
    enum Kind { VideoStream, AudioStream };

    MediaDecoder(Kind kind, QObject *parent = 0);
    ~MediaDecoder();

    // How the bytes are obtained. Native lets libavformat open the URL with
    // its own HTTP and TLS, which is what these XP FFmpeg builds carry and
    // what works when Qt has no OpenSSL. QtBridge feeds a custom AVIOContext
    // from QNetworkAccessManager instead, kept as the fallback for a build
    // where FFmpeg was compiled without a TLS backend.
    enum Transport { NativeIo, QtBridgeIo };

    // Opens on the calling thread, then start() runs the decode loop.
    bool openStream(const QString &url, QString *error, Transport transport = NativeIo);
    void stop();

    void requestSeek(double seconds);
    void setPaused(bool paused);

    // Sized so the scaler output matches the surface without a second
    // rescale in the paint event. Zero keeps the native size.
    void setTargetSize(const QSize &size);

    // The clock this decoder obeys. Video waits on it before showing a
    // frame; audio advances it as samples reach the sound card.
    void setSyncClock(SyncClock *clock) { sync_ = clock; }

    // When set and usable, frames go straight to the GPU as YUV and neither
    // swscale nor a QImage is involved at all.
    void setPresenter(D3D9Presenter *presenter) { presenter_ = presenter; }

    // Drops the deblocking filter when the machine cannot keep up. It is the
    // single biggest saving available in an H.264 decoder and costs some
    // sharpness, which is a better trade than dropping every third frame.
    void setLowLatencyMode(bool enabled);
    bool lowLatencyMode() const { return lowLatency_; }

    // Called by the GUI thread once a frame has actually been painted, so
    // the decoder knows the pipe is clear. Without this the queued signal
    // backs up and the interface stops responding.
    void acknowledgeFrame();

    double duration() const { return duration_; }
    int width() const { return width_; }
    int height() const { return height_; }
    double frameRate() const { return frameRate_; }
    QString codecName() const { return codecName_; }
    int droppedFrames() const { return dropped_; }

    // Audio only: interleaved signed 16-bit, the format every Windows sink
    // from waveOut upwards accepts without negotiation.
    int sampleRate() const { return sampleRate_; }
    int channelCount() const { return channels_; }
    QByteArray takeAudio(int maxBytes);
    int queuedAudioBytes() const;

    // The presentation clock. Audio reports what it has actually handed to
    // the sink; video reports the timestamp of the last frame emitted.
    double clock() const;

signals:
    void frameReady(const QImage &frame, double presentationTime);
    void audioReady();
    void openedStream(int width, int height, double duration);
    void endOfStream();
    void failed(const QString &message);
    void urlExpired();

protected:
    void run();

private:
    bool openCodec(AVFormatContext *format, int streamIndex);
    void closeAll();
    void emitVideoFrame(AVFrame *frame);
    bool waitUntilDue(double presentationTime);
    void queueAudio(AVFrame *frame);
    void performSeek();

    static int readPacket(void *opaque, unsigned char *buffer, int size);
    static qint64 seekPacket(void *opaque, qint64 offset, int whence);

    Kind kind_;
    MediaSource *source_;
    SyncClock *sync_;
    D3D9Presenter *presenter_;
    bool lowLatency_;
    int lateFrames_;
    QAtomicInt framesInFlight_;
    Transport transport_;

    AVFormatContext *format_;
    AVIOContext *avio_;
    AVCodecContext *codec_;
    SwsContext *scaler_;
    SwrContext *resampler_;
    AVFrame *frame_;
    AVFrame *scaled_;
    AVPacket *packet_;
    unsigned char *ioBuffer_;
    unsigned char *scaledBuffer_;
    int streamIndex_;

    int width_;
    int height_;
    int scaledWidth_;
    int scaledHeight_;
    double frameRate_;
    double duration_;
    double timeBase_;
    QString codecName_;
    int sampleRate_;
    int channels_;
    int dropped_;

    // Three rotating buffers rather than a fresh QImage per frame: at 1080p
    // a copy is eight megabytes, and at thirty frames a second that alone is
    // more memory bandwidth than these machines have to spare.
    QImage frameRing_[3];
    int ringIndex_;

    mutable QMutex mutex_;
    QWaitCondition wake_;
    QByteArray audioQueue_;
    double clock_;
    double seekTarget_;
    bool seekPending_;
    bool paused_;
    bool stopping_;
    QSize targetSize_;
};

#endif
