#ifndef SIGHTLINE_AUDIO_SINK_H
#define SIGHTLINE_AUDIO_SINK_H

#include <QByteArray>
#include <QObject>

// Pushes interleaved S16 to the speakers.
//
// Windows XP has no WASAPI, so this is DirectSound with a small ring buffer
// that is refilled from a timer. waveOut would also work but its latency is
// worse and it gives no play cursor, which the A/V clock needs.
//
// Built directly on dsound rather than QtMultimedia on purpose: the static
// Qt 5.6 build used here is not guaranteed to carry the multimedia module,
// and adding a dependency that might not be there would trade a known
// problem for an intermittent one.
class AudioSink : public QObject
{
    Q_OBJECT

public:
    explicit AudioSink(QObject *parent = 0);
    ~AudioSink();

    bool start(int sampleRate, int channels, QString *error = 0);
    void stop();
    bool isOpen() const { return opened_; }

    // Returns how many bytes were taken. Anything left over should be
    // offered again on the next tick rather than dropped.
    int write(const QByteArray &samples);

    void setVolume(int percent);
    void setPaused(bool paused);

    // Bytes still queued ahead of the play cursor, which is what the clock
    // has to subtract to know what the listener is actually hearing.
    int pendingBytes() const;
    double latencySeconds() const;

private:
    void *device_;          // LPDIRECTSOUND8
    void *buffer_;          // LPDIRECTSOUNDBUFFER
    int bufferBytes_;
    int writeCursor_;
    int sampleRate_;
    int channels_;
    int volume_;
    bool opened_;
    bool paused_;
};

#endif
