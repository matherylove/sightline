#include "audio_pump.h"

#include <QByteArray>

#include "audio_sink.h"
#include "media_decoder.h"

namespace {
// Five milliseconds. Short enough that the ring never runs dry between
// wakeups, long enough that the thread is asleep almost all the time.
const int kPumpIntervalMs = 5;
}

AudioPump::AudioPump(AudioSink *sink, QObject *parent)
    : QThread(parent), sink_(sink), decoder_(0), paused_(false), stopping_(false)
{
}

AudioPump::~AudioPump()
{
    stop();
}

void AudioPump::attach(MediaDecoder *decoder)
{
    QMutexLocker locker(&mutex_);
    decoder_ = decoder;
    locker.unlock();
    wake_.wakeAll();
}

void AudioPump::detach()
{
    QMutexLocker locker(&mutex_);
    decoder_ = 0;
}

void AudioPump::setPaused(bool paused)
{
    QMutexLocker locker(&mutex_);
    paused_ = paused;
    locker.unlock();
    wake_.wakeAll();
}

void AudioPump::stop()
{
    QMutexLocker locker(&mutex_);
    stopping_ = true;
    decoder_ = 0;
    locker.unlock();

    wake_.wakeAll();
    if (isRunning())
        wait(2000);
}

void AudioPump::run()
{
    while (true) {
        QMutexLocker locker(&mutex_);
        if (stopping_)
            return;

        MediaDecoder *decoder = decoder_;
        const bool paused = paused_;

        if (!decoder || paused || !sink_ || !sink_->isOpen()) {
            wake_.wait(&mutex_, 40);
            continue;
        }
        locker.unlock();

        // Keep roughly a quarter second queued ahead of the play cursor.
        // Less and a scheduling hiccup is audible; more and a seek leaves
        // stale audio playing after the picture has already moved.
        const int rate = decoder->sampleRate();
        const int channels = decoder->channelCount();
        if (rate <= 0 || channels <= 0) {
            msleep(kPumpIntervalMs);
            continue;
        }

        const int bytesPerSecond = rate * channels * 2;
        const int wanted = bytesPerSecond / 4;
        const int queued = sink_->pendingBytes();

        if (queued >= wanted) {
            msleep(kPumpIntervalMs);
            continue;
        }

        const QByteArray samples = decoder->takeAudio(wanted - queued);
        if (samples.isEmpty()) {
            // Decoder has nothing yet. Sleeping rather than spinning matters
            // on a single core machine, where a busy thread here starves the
            // decoder that is trying to produce the samples.
            msleep(kPumpIntervalMs);
            continue;
        }

        int written = sink_->write(samples);
        if (written < samples.size()) {
            // The ring was fuller than reported. Hold the remainder rather
            // than dropping it, or the stream develops a click.
            QByteArray rest = samples.mid(written);
            while (!rest.isEmpty()) {
                QMutexLocker check(&mutex_);
                if (stopping_ || !decoder_)
                    break;
                check.unlock();

                msleep(kPumpIntervalMs);
                written = sink_->write(rest);
                if (written <= 0)
                    continue;
                rest = rest.mid(written);
            }
        }
    }
}
