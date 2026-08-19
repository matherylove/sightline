#ifndef SIGHTLINE_AUDIO_PUMP_H
#define SIGHTLINE_AUDIO_PUMP_H

#include <QMutex>
#include <QThread>
#include <QWaitCondition>

class AudioSink;
class MediaDecoder;

// Moves decoded audio into the sound card on a thread of its own.
//
// This was previously done on the GUI thread, driven by a signal from the
// decoder. That is the wrong place for it and it is why playback stuttered:
// roughly fifty times a second the interface thread had to take the
// DirectSound buffer lock, so any repaint — a hovered card, a status bar
// update — stalled the write. The audio clock stalls with it, and because
// the video paces itself against that clock, the picture jerks.
//
// Every serious player keeps audio on a dedicated thread for exactly this
// reason. The pump wakes on a short interval, tops up the ring buffer with
// whatever the decoder has ready, and never touches a widget.
class AudioPump : public QThread
{
    Q_OBJECT

public:
    AudioPump(AudioSink *sink, QObject *parent = 0);
    ~AudioPump();

    void attach(MediaDecoder *decoder);
    void detach();
    void setPaused(bool paused);
    void stop();

protected:
    void run();

private:
    AudioSink *sink_;
    MediaDecoder *decoder_;
    mutable QMutex mutex_;
    QWaitCondition wake_;
    bool paused_;
    bool stopping_;
};

#endif
