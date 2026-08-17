#include "media_decoder.h"

#include <QSize>

#include "media_source.h"

extern "C" {
// Listed explicitly rather than relying on avformat.h and avcodec.h dragging
// them in: the transitive set has shifted between FFmpeg majors before, and a
// missing macro shows up as a confusing "undeclared identifier" much later.
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

// 32 KB per AVIO read. Larger buffers do not help over a custom context and
// they lengthen the stall when a seek invalidates what was already fetched.
const int kIoBufferSize = 32 * 1024;

// Roughly two seconds of stereo at 48 kHz. Past this the decoder waits for
// the sink to drain instead of growing the queue without bound.
const int kAudioQueueLimit = 48000 * 2 * 2 * 2;

QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

} // namespace

MediaDecoder::MediaDecoder(Kind kind, QObject *parent)
    : QThread(parent),
      kind_(kind), source_(0),
      format_(0), avio_(0), codec_(0), scaler_(0), resampler_(0),
      frame_(0), scaled_(0), packet_(0), ioBuffer_(0), scaledBuffer_(0),
      streamIndex_(-1),
      width_(0), height_(0), scaledWidth_(0), scaledHeight_(0),
      frameRate_(0.0), duration_(0.0), timeBase_(0.0),
      sampleRate_(0), channels_(0), dropped_(0),
      clock_(0.0), seekTarget_(0.0), seekPending_(false),
      paused_(false), stopping_(false)
{
    source_ = new MediaSource;
}

MediaDecoder::~MediaDecoder()
{
    stop();
    closeAll();
    delete source_;
}

int MediaDecoder::readPacket(void *opaque, unsigned char *buffer, int size)
{
    MediaDecoder *self = static_cast<MediaDecoder *>(opaque);
    return self->source_->readAt(buffer, size);
}

qint64 MediaDecoder::seekPacket(void *opaque, qint64 offset, int whence)
{
    MediaDecoder *self = static_cast<MediaDecoder *>(opaque);
    return self->source_->seekTo(offset, whence);
}

bool MediaDecoder::openStream(const QString &url, QString *error)
{
    closeAll();

    source_->open(url);

    ioBuffer_ = static_cast<unsigned char *>(av_malloc(kIoBufferSize));
    if (!ioBuffer_) {
        if (error) *error = QString::fromUtf8("Sin memoria para el búfer de entrada.");
        return false;
    }

    avio_ = avio_alloc_context(ioBuffer_, kIoBufferSize, 0, this,
                               &MediaDecoder::readPacket, 0, &MediaDecoder::seekPacket);
    if (!avio_) {
        av_freep(&ioBuffer_);
        if (error) *error = QString::fromUtf8("No se pudo crear el contexto de E/S.");
        return false;
    }

    format_ = avformat_alloc_context();
    if (!format_) {
        if (error) *error = QString::fromUtf8("No se pudo reservar el demuxer.");
        return false;
    }
    format_->pb = avio_;
    format_->flags |= AVFMT_FLAG_CUSTOM_IO;

    // The adaptive files are fragmented MP4 or WebM with a moov at the front,
    // so a small probe is enough and keeps the first frame quick.
    format_->probesize = 512 * 1024;
    format_->max_analyze_duration = 3 * AV_TIME_BASE;

    int result = avformat_open_input(&format_, "", 0, 0);
    if (result < 0) {
        if (error) *error = QString::fromUtf8("No se pudo abrir el stream: ") + avError(result);
        return false;
    }

    result = avformat_find_stream_info(format_, 0);
    if (result < 0) {
        if (error) *error = QString::fromUtf8("Sin información de stream: ") + avError(result);
        return false;
    }

    const AVMediaType wanted = (kind_ == VideoStream) ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;
    streamIndex_ = av_find_best_stream(format_, wanted, -1, -1, 0, 0);
    if (streamIndex_ < 0) {
        if (error) {
            *error = (kind_ == VideoStream)
                ? QString::fromUtf8("El archivo no contiene vídeo.")
                : QString::fromUtf8("El archivo no contiene audio.");
        }
        return false;
    }

    if (!openCodec(format_, streamIndex_)) {
        if (error) *error = QString::fromUtf8("No hay decodificador para este formato.");
        return false;
    }

    AVStream *stream = format_->streams[streamIndex_];
    timeBase_ = av_q2d(stream->time_base);

    if (stream->duration > 0)
        duration_ = stream->duration * timeBase_;
    else if (format_->duration > 0)
        duration_ = double(format_->duration) / AV_TIME_BASE;

    if (kind_ == VideoStream) {
        width_ = codec_->width;
        height_ = codec_->height;
        const AVRational guessed = av_guess_frame_rate(format_, stream, 0);
        frameRate_ = guessed.den > 0 ? av_q2d(guessed) : 0.0;
    } else {
        sampleRate_ = codec_->sample_rate;
        channels_ = codec_->ch_layout.nb_channels;
        if (channels_ <= 0)
            channels_ = 2;

        // Everything downstream wants interleaved S16 at the source rate:
        // that is what DirectSound and waveOut take on XP without a mixer
        // in between, and it is cheap for an old CPU to push around.
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, qMin(channels_, 2));

        result = swr_alloc_set_opts2(&resampler_,
                                     &outLayout, AV_SAMPLE_FMT_S16, sampleRate_,
                                     &codec_->ch_layout, codec_->sample_fmt, codec_->sample_rate,
                                     0, 0);
        av_channel_layout_uninit(&outLayout);

        if (result < 0 || swr_init(resampler_) < 0) {
            if (error) *error = QString::fromUtf8("No se pudo preparar el remuestreo de audio.");
            return false;
        }
        channels_ = qMin(channels_, 2);
    }

    codecName_ = QString::fromUtf8(avcodec_get_name(codec_->codec_id));

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) {
        if (error) *error = QString::fromUtf8("Sin memoria para los búferes de decodificación.");
        return false;
    }

    emit openedStream(width_, height_, duration_);
    return true;
}

bool MediaDecoder::openCodec(AVFormatContext *format, int streamIndex)
{
    AVStream *stream = format->streams[streamIndex];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder)
        return false;

    codec_ = avcodec_alloc_context3(decoder);
    if (!codec_)
        return false;

    if (avcodec_parameters_to_context(codec_, stream->codecpar) < 0)
        return false;

    codec_->pkt_timebase = stream->time_base;

    // Frame threading on however many cores the machine has. On a Pentium 4
    // that is one and this costs nothing; on the Core 2 and later boxes that
    // XP Integral Edition tends to run on, it is the difference between 720p
    // playing and 720p stuttering.
    codec_->thread_count = 0;
    codec_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    return avcodec_open2(codec_, decoder, 0) >= 0;
}

void MediaDecoder::closeAll()
{
    if (codec_)
        avcodec_free_context(&codec_);

    if (format_) {
        // The AVIOContext is ours, not libavformat's, so its buffer has to be
        // released by hand after the context is closed or it leaks per open.
        avformat_close_input(&format_);
    }

    if (avio_) {
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
        ioBuffer_ = 0;
    }
    if (ioBuffer_)
        av_freep(&ioBuffer_);

    if (scaler_) {
        sws_freeContext(scaler_);
        scaler_ = 0;
    }
    if (resampler_)
        swr_free(&resampler_);
    if (frame_)
        av_frame_free(&frame_);
    if (scaled_)
        av_frame_free(&scaled_);
    if (packet_)
        av_packet_free(&packet_);
    if (scaledBuffer_)
        av_freep(&scaledBuffer_);

    streamIndex_ = -1;
}

void MediaDecoder::stop()
{
    QMutexLocker locker(&mutex_);
    stopping_ = true;
    locker.unlock();

    wake_.wakeAll();
    if (source_)
        source_->close();

    if (isRunning())
        wait(3000);
}

void MediaDecoder::requestSeek(double seconds)
{
    QMutexLocker locker(&mutex_);
    seekTarget_ = seconds;
    seekPending_ = true;
    audioQueue_.clear();
    locker.unlock();
    wake_.wakeAll();
}

void MediaDecoder::setPaused(bool paused)
{
    QMutexLocker locker(&mutex_);
    paused_ = paused;
    locker.unlock();
    wake_.wakeAll();
}

void MediaDecoder::setTargetSize(const QSize &size)
{
    QMutexLocker locker(&mutex_);
    targetSize_ = size;
}

double MediaDecoder::clock() const
{
    QMutexLocker locker(&mutex_);
    return clock_;
}

int MediaDecoder::queuedAudioBytes() const
{
    QMutexLocker locker(&mutex_);
    return audioQueue_.size();
}

QByteArray MediaDecoder::takeAudio(int maxBytes)
{
    QMutexLocker locker(&mutex_);
    if (audioQueue_.isEmpty())
        return QByteArray();

    const int count = qMin(maxBytes, audioQueue_.size());
    const QByteArray chunk = audioQueue_.left(count);
    audioQueue_.remove(0, count);

    // The clock advances as bytes leave for the sink, not as they are
    // decoded: that is what makes audio the master and keeps video honest.
    if (sampleRate_ > 0 && channels_ > 0)
        clock_ += double(count) / double(sampleRate_ * channels_ * 2);

    locker.unlock();
    wake_.wakeAll();
    return chunk;
}

void MediaDecoder::performSeek()
{
    QMutexLocker locker(&mutex_);
    const double target = seekTarget_;
    seekPending_ = false;
    audioQueue_.clear();
    clock_ = target;
    locker.unlock();

    const qint64 timestamp = qint64(target / (timeBase_ > 0.0 ? timeBase_ : 1.0));
    if (av_seek_frame(format_, streamIndex_, timestamp, AVSEEK_FLAG_BACKWARD) >= 0)
        avcodec_flush_buffers(codec_);
}

void MediaDecoder::emitVideoFrame(AVFrame *frame)
{
    QMutexLocker locker(&mutex_);
    QSize target = targetSize_;
    locker.unlock();

    int outWidth = target.isEmpty() ? frame->width : target.width();
    int outHeight = target.isEmpty() ? frame->height : target.height();

    // Never scale up: enlarging in software costs real time on these CPUs
    // and the surface can stretch a smaller image for free when it blits.
    if (outWidth > frame->width || outHeight > frame->height) {
        outWidth = frame->width;
        outHeight = frame->height;
    }
    outWidth &= ~1;
    outHeight &= ~1;
    if (outWidth <= 0 || outHeight <= 0)
        return;

    if (!scaler_ || outWidth != scaledWidth_ || outHeight != scaledHeight_) {
        if (scaler_)
            sws_freeContext(scaler_);

        // Bilinear, not bicubic: on a Pentium 4 the quality difference is
        // invisible at these sizes and the cost is not.
        scaler_ = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format),
                                 outWidth, outHeight, AV_PIX_FMT_BGRA,
                                 SWS_BILINEAR, 0, 0, 0);
        if (!scaler_)
            return;

        if (scaled_)
            av_frame_free(&scaled_);
        if (scaledBuffer_)
            av_freep(&scaledBuffer_);

        scaled_ = av_frame_alloc();
        const int bytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, outWidth, outHeight, 4);
        scaledBuffer_ = static_cast<unsigned char *>(av_malloc(bytes));
        av_image_fill_arrays(scaled_->data, scaled_->linesize, scaledBuffer_,
                             AV_PIX_FMT_BGRA, outWidth, outHeight, 4);

        scaledWidth_ = outWidth;
        scaledHeight_ = outHeight;
    }

    sws_scale(scaler_, frame->data, frame->linesize, 0, frame->height,
              scaled_->data, scaled_->linesize);

    // BGRA in memory is Format_RGB32 in Qt on a little-endian machine, so
    // this wraps without a per-pixel pass. The copy is what makes it safe to
    // hand across the thread boundary.
    QImage image(scaled_->data[0], scaledWidth_, scaledHeight_,
                 scaled_->linesize[0], QImage::Format_RGB32);

    double presentation = 0.0;
    if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
        presentation = frame->best_effort_timestamp * timeBase_;
    else if (frame->pts != AV_NOPTS_VALUE)
        presentation = frame->pts * timeBase_;

    QMutexLocker clockLocker(&mutex_);
    clock_ = presentation;
    clockLocker.unlock();

    emit frameReady(image.copy(), presentation);
}

void MediaDecoder::queueAudio(AVFrame *frame)
{
    const int outSamples = swr_get_out_samples(resampler_, frame->nb_samples);
    if (outSamples <= 0)
        return;

    QByteArray buffer;
    buffer.resize(outSamples * channels_ * 2);

    unsigned char *planes[1];
    planes[0] = reinterpret_cast<unsigned char *>(buffer.data());

    const int converted = swr_convert(resampler_, planes, outSamples,
                                      const_cast<const unsigned char **>(frame->extended_data),
                                      frame->nb_samples);
    if (converted <= 0)
        return;

    buffer.resize(converted * channels_ * 2);

    QMutexLocker locker(&mutex_);
    audioQueue_.append(buffer);
    locker.unlock();

    emit audioReady();
}

void MediaDecoder::run()
{
    if (!format_ || !codec_)
        return;

    bool reachedEnd = false;

    while (true) {
        QMutexLocker locker(&mutex_);
        if (stopping_)
            break;

        if (seekPending_) {
            locker.unlock();
            performSeek();
            reachedEnd = false;
            continue;
        }

        // Back off when the sink is well ahead or playback is paused, rather
        // than spinning: on a single-core machine a busy decoder thread
        // starves the interface.
        const bool waitForRoom = (kind_ == AudioStream && audioQueue_.size() > kAudioQueueLimit);
        if (paused_ || waitForRoom || reachedEnd) {
            wake_.wait(&mutex_, 50);
            locker.unlock();
            continue;
        }
        locker.unlock();

        if (source_->expired()) {
            emit urlExpired();
            break;
        }

        const int result = av_read_frame(format_, packet_);
        if (result < 0) {
            if (result == AVERROR_EOF || source_->finished()) {
                // Flush whatever the decoder is still holding before saying
                // the stream is over, or the last frames are simply lost.
                avcodec_send_packet(codec_, 0);
                while (avcodec_receive_frame(codec_, frame_) >= 0) {
                    if (kind_ == VideoStream)
                        emitVideoFrame(frame_);
                    else
                        queueAudio(frame_);
                    av_frame_unref(frame_);
                }
                reachedEnd = true;
                emit endOfStream();
                continue;
            }
            if (result == AVERROR(EAGAIN)) {
                msleep(10);
                continue;
            }
            emit failed(QString::fromUtf8("Error de lectura: ") + avError(result));
            break;
        }

        if (packet_->stream_index != streamIndex_) {
            av_packet_unref(packet_);
            continue;
        }

        const int sent = avcodec_send_packet(codec_, packet_);
        av_packet_unref(packet_);

        if (sent < 0 && sent != AVERROR(EAGAIN)) {
            ++dropped_;
            continue;
        }

        while (true) {
            const int received = avcodec_receive_frame(codec_, frame_);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
                break;
            if (received < 0) {
                ++dropped_;
                break;
            }

            if (kind_ == VideoStream)
                emitVideoFrame(frame_);
            else
                queueAudio(frame_);

            av_frame_unref(frame_);
        }
    }
}
