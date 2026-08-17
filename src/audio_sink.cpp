#include "audio_sink.h"

#include <QString>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dsound.h>
#endif

namespace {
// Half a second of stereo at 48 kHz. Small enough that a seek does not leave
// stale audio playing, large enough to ride out a scheduling hiccup on a
// single-core machine.
const int kBufferSeconds = 2;
}

AudioSink::AudioSink(QObject *parent)
    : QObject(parent),
      device_(0), buffer_(0), bufferBytes_(0), writeCursor_(0),
      sampleRate_(0), channels_(0), volume_(80), opened_(false), paused_(false)
{
}

AudioSink::~AudioSink()
{
    stop();
}

#ifdef Q_OS_WIN

bool AudioSink::start(int sampleRate, int channels, QString *error)
{
    stop();

    if (sampleRate <= 0 || channels <= 0) {
        if (error) *error = QString::fromUtf8("Formato de audio inválido.");
        return false;
    }

    LPDIRECTSOUND8 device = 0;
    if (FAILED(DirectSoundCreate8(0, &device, 0))) {
        if (error) *error = QString::fromUtf8("No se pudo abrir DirectSound.");
        return false;
    }

    // Priority, not exclusive: the user should still hear other programs,
    // and exclusive mode on XP tends to fight with the system mixer.
    if (FAILED(device->SetCooperativeLevel(GetDesktopWindow(), DSSCL_PRIORITY))) {
        device->Release();
        if (error) *error = QString::fromUtf8("DirectSound rechazó el nivel de cooperación.");
        return false;
    }

    WAVEFORMATEX format;
    ZeroMemory(&format, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = WORD(channels);
    format.nSamplesPerSec = DWORD(sampleRate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = WORD(channels * 2);
    format.nAvgBytesPerSec = DWORD(sampleRate * channels * 2);

    bufferBytes_ = sampleRate * channels * 2 * kBufferSeconds;

    DSBUFFERDESC description;
    ZeroMemory(&description, sizeof(description));
    description.dwSize = sizeof(description);
    description.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2
                        | DSBCAPS_CTRLVOLUME;
    description.dwBufferBytes = DWORD(bufferBytes_);
    description.lpwfxFormat = &format;

    LPDIRECTSOUNDBUFFER buffer = 0;
    if (FAILED(device->CreateSoundBuffer(&description, &buffer, 0))) {
        device->Release();
        if (error) *error = QString::fromUtf8("No se pudo crear el búfer de audio.");
        return false;
    }

    // Start from silence so the first write is not preceded by whatever was
    // left in the freshly allocated buffer.
    void *block = 0;
    DWORD blockSize = 0;
    if (SUCCEEDED(buffer->Lock(0, 0, &block, &blockSize, 0, 0, DSBLOCK_ENTIREBUFFER))) {
        ZeroMemory(block, blockSize);
        buffer->Unlock(block, blockSize, 0, 0);
    }

    device_ = device;
    buffer_ = buffer;
    sampleRate_ = sampleRate;
    channels_ = channels;
    writeCursor_ = 0;
    opened_ = true;
    paused_ = false;

    setVolume(volume_);
    buffer->Play(0, 0, DSBPLAY_LOOPING);
    return true;
}

void AudioSink::stop()
{
    if (buffer_) {
        LPDIRECTSOUNDBUFFER buffer = static_cast<LPDIRECTSOUNDBUFFER>(buffer_);
        buffer->Stop();
        buffer->Release();
        buffer_ = 0;
    }
    if (device_) {
        static_cast<LPDIRECTSOUND8>(device_)->Release();
        device_ = 0;
    }
    opened_ = false;
    writeCursor_ = 0;
}

int AudioSink::write(const QByteArray &samples)
{
    if (!opened_ || samples.isEmpty())
        return 0;

    LPDIRECTSOUNDBUFFER buffer = static_cast<LPDIRECTSOUNDBUFFER>(buffer_);

    DWORD playCursor = 0;
    DWORD safeCursor = 0;
    if (FAILED(buffer->GetCurrentPosition(&playCursor, &safeCursor)))
        return 0;

    // Never write into the region the hardware is about to read: that is
    // what produces the classic ring-buffer crackle.
    int free = int(playCursor) - writeCursor_;
    if (free <= 0)
        free += bufferBytes_;
    free -= int(sampleRate_ * channels_ * 2 / 20);   // 50 ms of guard
    if (free <= 0)
        return 0;

    const int count = qMin(samples.size(), free);

    void *first = 0, *second = 0;
    DWORD firstSize = 0, secondSize = 0;

    if (FAILED(buffer->Lock(DWORD(writeCursor_), DWORD(count),
                            &first, &firstSize, &second, &secondSize, 0)))
        return 0;

    memcpy(first, samples.constData(), firstSize);
    if (second && secondSize > 0)
        memcpy(second, samples.constData() + firstSize, secondSize);

    buffer->Unlock(first, firstSize, second, secondSize);

    writeCursor_ = (writeCursor_ + count) % bufferBytes_;
    return count;
}

int AudioSink::pendingBytes() const
{
    if (!opened_)
        return 0;

    LPDIRECTSOUNDBUFFER buffer = static_cast<LPDIRECTSOUNDBUFFER>(buffer_);
    DWORD playCursor = 0;
    if (FAILED(buffer->GetCurrentPosition(&playCursor, 0)))
        return 0;

    int pending = writeCursor_ - int(playCursor);
    if (pending < 0)
        pending += bufferBytes_;
    return pending;
}

void AudioSink::setVolume(int percent)
{
    volume_ = qBound(0, percent, 100);
    if (!opened_)
        return;

    // DirectSound volume is hundredths of a decibel of attenuation, so a
    // linear slider has to be converted or the top half does nothing.
    LONG attenuation = DSBVOLUME_MIN;
    if (volume_ > 0) {
        const double ratio = volume_ / 100.0;
        attenuation = LONG(2000.0 * log10(ratio));
        if (attenuation < DSBVOLUME_MIN) attenuation = DSBVOLUME_MIN;
        if (attenuation > DSBVOLUME_MAX) attenuation = DSBVOLUME_MAX;
    }
    static_cast<LPDIRECTSOUNDBUFFER>(buffer_)->SetVolume(attenuation);
}

void AudioSink::setPaused(bool paused)
{
    if (!opened_ || paused_ == paused)
        return;
    paused_ = paused;

    LPDIRECTSOUNDBUFFER buffer = static_cast<LPDIRECTSOUNDBUFFER>(buffer_);
    if (paused)
        buffer->Stop();
    else
        buffer->Play(0, 0, DSBPLAY_LOOPING);
}

#else

// Non-Windows builds exist only so the tree compiles on a developer machine;
// there is no sink there and the clock falls back to wall time.
bool AudioSink::start(int, int, QString *error)
{
    if (error) *error = QString::fromUtf8("Salida de audio no disponible en esta plataforma.");
    return false;
}
void AudioSink::stop() { opened_ = false; }
int AudioSink::write(const QByteArray &) { return 0; }
int AudioSink::pendingBytes() const { return 0; }
void AudioSink::setVolume(int percent) { volume_ = percent; }
void AudioSink::setPaused(bool paused) { paused_ = paused; }

#endif

double AudioSink::latencySeconds() const
{
    if (!opened_ || sampleRate_ <= 0 || channels_ <= 0)
        return 0.0;
    return double(pendingBytes()) / double(sampleRate_ * channels_ * 2);
}
