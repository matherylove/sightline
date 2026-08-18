#ifndef SIGHTLINE_SYNC_CLOCK_H
#define SIGHTLINE_SYNC_CLOCK_H

#include <QElapsedTimer>
#include <QMutex>

// The presentation clock, shared between the audio thread that advances it
// and the video thread that has to obey it.
//
// Without this the video decoder emits every frame the instant it is decoded,
// so an eighteen minute video plays in whatever time the CPU needs to chew
// through it. Frame pacing is not an optimisation here; it is the difference
// between playback and a flipbook.
class SyncClock
{
public:
    SyncClock();

    // Set by whoever owns the master clock: the audio path when there is a
    // sound track, the video path when there is not.
    void set(double seconds);
    double get() const;

    // Used when no audio exists: the clock free-runs from a monotonic timer
    // anchored at the last set() call, so video still plays at real speed.
    void startFreeRun(double fromSeconds);
    void stopFreeRun();
    bool freeRunning() const;

    void setPaused(bool paused);
    bool paused() const;
    void reset(double seconds);

private:
    mutable QMutex mutex_;
    double value_;
    bool paused_;
    bool freeRun_;
    QElapsedTimer timer_;
    double anchor_;
};

#endif
