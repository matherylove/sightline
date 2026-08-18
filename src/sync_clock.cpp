#include "sync_clock.h"

SyncClock::SyncClock()
    : value_(0.0), paused_(false), freeRun_(false), anchor_(0.0)
{
}

void SyncClock::set(double seconds)
{
    QMutexLocker locker(&mutex_);
    value_ = seconds;
    if (freeRun_) {
        anchor_ = seconds;
        timer_.restart();
    }
}

double SyncClock::get() const
{
    QMutexLocker locker(&mutex_);
    if (freeRun_ && !paused_ && timer_.isValid())
        return anchor_ + timer_.elapsed() / 1000.0;
    return value_;
}

void SyncClock::startFreeRun(double fromSeconds)
{
    QMutexLocker locker(&mutex_);
    freeRun_ = true;
    anchor_ = fromSeconds;
    value_ = fromSeconds;
    timer_.start();
}

void SyncClock::stopFreeRun()
{
    QMutexLocker locker(&mutex_);
    if (freeRun_ && timer_.isValid())
        value_ = anchor_ + timer_.elapsed() / 1000.0;
    freeRun_ = false;
}

bool SyncClock::freeRunning() const
{
    QMutexLocker locker(&mutex_);
    return freeRun_;
}

void SyncClock::setPaused(bool paused)
{
    QMutexLocker locker(&mutex_);
    if (paused_ == paused)
        return;

    // Freezing the free-running clock means folding the elapsed time into
    // the anchor, or the picture jumps forward on resume by however long
    // the pause lasted.
    if (freeRun_ && timer_.isValid()) {
        if (paused) {
            anchor_ += timer_.elapsed() / 1000.0;
            value_ = anchor_;
        } else {
            timer_.restart();
        }
    }
    paused_ = paused;
}

bool SyncClock::paused() const
{
    QMutexLocker locker(&mutex_);
    return paused_;
}

void SyncClock::reset(double seconds)
{
    QMutexLocker locker(&mutex_);
    value_ = seconds;
    anchor_ = seconds;
    if (freeRun_)
        timer_.restart();
}
