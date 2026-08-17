#include "media_types.h"

#include <QLocale>

namespace {

QString spanishThousands(qint64 value)
{
    // Spanish groups with a thin space rather than a comma, and QLocale on a
    // machine set to English would get this wrong, so it is done by hand.
    const QString digits = QString::number(value);
    QString out;
    int count = 0;
    for (int i = digits.size() - 1; i >= 0; --i) {
        out.prepend(digits.at(i));
        if (++count % 3 == 0 && i > 0)
            out.prepend(QChar(0x202F));
    }
    return out;
}

QString compactCount(qint64 value)
{
    if (value < 1000)
        return QString::number(value);
    if (value < 1000000) {
        const double thousands = value / 1000.0;
        if (thousands < 10.0)
            return QString::fromLatin1("%1 mil").arg(QString::number(thousands, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')));
        return QString::fromLatin1("%1 mil").arg(qint64(thousands));
    }
    const double millions = value / 1000000.0;
    if (millions < 10.0)
        return QString::fromLatin1("%1 M").arg(QString::number(millions, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')));
    return QString::fromLatin1("%1 M").arg(qint64(millions));
}

} // namespace

// ---------------------------------------------------------------- MediaFormat

MediaFormat::MediaFormat()
    : width(0), height(0), fps(0.0), bitrate(0), fileSize(0)
{
}

bool MediaFormat::hasVideo() const
{
    return !videoCodec.isEmpty() && videoCodec != QLatin1String("none");
}

bool MediaFormat::hasAudio() const
{
    return !audioCodec.isEmpty() && audioCodec != QLatin1String("none");
}

bool MediaFormat::isAvc() const
{
    return videoCodec.startsWith(QLatin1String("avc"), Qt::CaseInsensitive)
        || videoCodec.startsWith(QLatin1String("h264"), Qt::CaseInsensitive);
}

QString MediaFormat::qualityLabel() const
{
    if (hasVideo() && height > 0) {
        QString label = QString::number(height) + QLatin1Char('p');
        if (fps >= 50.0)
            label += QString::number(qRound(fps));
        return label;
    }
    if (hasAudio()) {
        QString codec = QString::fromLatin1("Audio");
        if (audioCodec.startsWith(QLatin1String("mp4a")))
            codec = QString::fromLatin1("AAC");
        else if (audioCodec.startsWith(QLatin1String("opus")))
            codec = QString::fromLatin1("Opus");
        else if (audioCodec.startsWith(QLatin1String("vorbis")))
            codec = QString::fromLatin1("Vorbis");
        if (bitrate > 0)
            return QString::fromLatin1("%1 %2 kb/s").arg(codec).arg(bitrate / 1000);
        return codec;
    }
    return note;
}

QString MediaFormat::bitrateLabel() const
{
    if (bitrate <= 0)
        return QString();
    if (bitrate >= 1000000) {
        const double mbps = bitrate / 1000000.0;
        return QString::fromLatin1("%1 Mb/s")
            .arg(QString::number(mbps, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')));
    }
    return QString::fromLatin1("%1 kb/s").arg(bitrate / 1000);
}

QString MediaFormat::sizeLabel() const
{
    if (fileSize <= 0)
        return QString();
    if (fileSize >= 1024LL * 1024LL * 1024LL) {
        const double gb = fileSize / (1024.0 * 1024.0 * 1024.0);
        return QString::fromLatin1("%1 GB")
            .arg(QString::number(gb, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')));
    }
    return QString::fromLatin1("%1 MB").arg(fileSize / (1024LL * 1024LL));
}

// ------------------------------------------------------------ SponsorSegment

SponsorSegment::SponsorSegment()
    : category(UnknownCategory), start(0.0), end(0.0), skipped(false)
{
}

SponsorSegment::Category SponsorSegment::categoryFromApi(const QString &name)
{
    if (name == QLatin1String("sponsor"))          return Sponsor;
    if (name == QLatin1String("intro"))            return Intro;
    if (name == QLatin1String("outro"))            return Intro;
    if (name == QLatin1String("selfpromo"))        return SelfPromo;
    if (name == QLatin1String("interaction"))      return Interaction;
    if (name == QLatin1String("music_offtopic"))   return MusicOffTopic;
    if (name == QLatin1String("preview"))          return Preview;
    if (name == QLatin1String("filler"))           return Filler;
    return UnknownCategory;
}

QString SponsorSegment::apiName(Category category)
{
    switch (category) {
    case Sponsor:        return QString::fromLatin1("sponsor");
    case Intro:          return QString::fromLatin1("intro");
    case SelfPromo:      return QString::fromLatin1("selfpromo");
    case Interaction:    return QString::fromLatin1("interaction");
    case MusicOffTopic:  return QString::fromLatin1("music_offtopic");
    case Preview:        return QString::fromLatin1("preview");
    case Filler:         return QString::fromLatin1("filler");
    default:             return QString::fromLatin1("unknown");
    }
}

QString SponsorSegment::displayName(Category category)
{
    switch (category) {
    case Sponsor:        return QString::fromUtf8("Patrocinio");
    case Intro:          return QString::fromUtf8("Intro y cortinilla");
    case SelfPromo:      return QString::fromUtf8("Autopromoción");
    case Interaction:    return QString::fromUtf8("Recordatorio");
    case MusicOffTopic:  return QString::fromUtf8("Escena musical");
    case Preview:        return QString::fromUtf8("Resumen o avance");
    case Filler:         return QString::fromUtf8("Relleno");
    default:             return QString::fromUtf8("Otro");
    }
}

QString SponsorSegment::description(Category category)
{
    switch (category) {
    case Sponsor:        return QString::fromUtf8("Promoción pagada, códigos de descuento");
    case Intro:          return QString::fromUtf8("Animación sin contenido");
    case SelfPromo:      return QString::fromUtf8("Su propio Patreon, mercancía, canal");
    case Interaction:    return QString::fromUtf8("«Suscríbete y dale a la campana»");
    case MusicOffTopic:  return QString::fromUtf8("Solo en vídeos de música");
    case Preview:        return QString::fromUtf8("Adelanto de lo que viene después");
    case Filler:         return QString::fromUtf8("Chistes y tangentes sin relación");
    default:             return QString();
    }
}

// -------------------------------------------------------------- VideoComment

VideoComment::VideoComment()
    : likeCount(0), replyCount(0), authorIsUploader(false), pinned(false)
{
}

// -------------------------------------------------------------- VideoChapter

VideoChapter::VideoChapter()
    : start(0.0), end(0.0)
{
}

// ----------------------------------------------------------------- VideoItem

VideoItem::VideoItem()
    : duration(0), viewCount(0), likeCount(0), detailed(false), resumePosition(0)
{
}

QString VideoItem::watchUrl() const
{
    return QString::fromLatin1("https://www.youtube.com/watch?v=") + id;
}

QString VideoItem::durationLabel() const
{
    if (duration <= 0)
        return QString::fromLatin1("--:--");
    const qint64 hours = duration / 3600;
    const qint64 minutes = (duration % 3600) / 60;
    const qint64 seconds = duration % 60;
    if (hours > 0) {
        return QString::fromLatin1("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QString::fromLatin1("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString VideoItem::viewCountLabel() const
{
    if (viewCount <= 0)
        return QString();
    return compactCount(viewCount);
}

QString VideoItem::publishedLabel() const
{
    if (!published.isValid())
        return QString();

    const qint64 seconds = published.secsTo(QDateTime::currentDateTimeUtc());
    if (seconds < 60)
        return QString::fromUtf8("ahora mismo");
    if (seconds < 3600)
        return QString::fromUtf8("hace %1 min").arg(seconds / 60);
    if (seconds < 86400)
        return QString::fromUtf8("hace %1 h").arg(seconds / 3600);

    const qint64 days = seconds / 86400;
    if (days < 7)
        return QString::fromUtf8("hace %1 d").arg(days);
    if (days < 31)
        return QString::fromUtf8("hace %1 sem.").arg(days / 7);
    if (days < 365)
        return QString::fromUtf8("hace %1 meses").arg(days / 30);
    const qint64 years = days / 365;
    return years == 1 ? QString::fromUtf8("hace 1 año")
                      : QString::fromUtf8("hace %1 años").arg(years);
}

bool VideoItem::urlsLikelyExpired() const
{
    if (!urlsFetchedAt.isValid())
        return true;
    // Five hours, not six: re-extracting a minute early costs one process,
    // re-extracting a minute late costs a stall in the middle of playback.
    return urlsFetchedAt.secsTo(QDateTime::currentDateTimeUtc()) > 5 * 3600;
}

const MediaFormat *VideoItem::selectedVideo() const
{
    for (int i = 0; i < requested.size(); ++i) {
        if (requested.at(i).hasVideo())
            return &requested.at(i);
    }
    return 0;
}

const MediaFormat *VideoItem::selectedAudio() const
{
    // A muxed file carries both, and yt-dlp returns it as a single entry;
    // in that case the video format is also the audio format.
    for (int i = 0; i < requested.size(); ++i) {
        if (requested.at(i).hasAudio() && !requested.at(i).hasVideo())
            return &requested.at(i);
    }
    for (int i = 0; i < requested.size(); ++i) {
        if (requested.at(i).hasAudio())
            return &requested.at(i);
    }
    return 0;
}

const MediaFormat *VideoItem::bestVideoFormat(int maxHeight, bool avcOnly) const
{
    const MediaFormat *best = 0;
    for (int i = 0; i < formats.size(); ++i) {
        const MediaFormat &format = formats.at(i);
        if (!format.hasVideo())
            continue;
        if (avcOnly && !format.isAvc())
            continue;
        if (maxHeight > 0 && format.height > maxHeight)
            continue;
        if (!best || format.height > best->height
            || (format.height == best->height && format.bitrate > best->bitrate)) {
            best = &format;
        }
    }
    return best;
}

const MediaFormat *VideoItem::bestAudioFormat(bool preferAac) const
{
    const MediaFormat *best = 0;
    for (int i = 0; i < formats.size(); ++i) {
        const MediaFormat &format = formats.at(i);
        if (!format.hasAudio() || format.hasVideo())
            continue;
        const bool isAac = format.audioCodec.startsWith(QLatin1String("mp4a"));
        if (!best) {
            best = &format;
            continue;
        }
        const bool bestIsAac = best->audioCodec.startsWith(QLatin1String("mp4a"));
        if (preferAac && isAac != bestIsAac) {
            if (isAac)
                best = &format;
            continue;
        }
        if (format.bitrate > best->bitrate)
            best = &format;
    }
    return best;
}

const MediaFormat *VideoItem::formatByItag(const QString &itag) const
{
    for (int i = 0; i < formats.size(); ++i) {
        if (formats.at(i).itag == itag)
            return &formats.at(i);
    }
    return 0;
}

// --------------------------------------------------------------- ChannelItem

ChannelItem::ChannelItem()
    : subscriberCount(0), unwatchedCount(0)
{
}

QString ChannelItem::url() const
{
    if (!handle.isEmpty())
        return QString::fromLatin1("https://www.youtube.com/") + handle;
    return QString::fromLatin1("https://www.youtube.com/channel/") + id;
}

QString ChannelItem::subscriberLabel() const
{
    if (subscriberCount <= 0)
        return QString();
    return compactCount(subscriberCount) + QString::fromUtf8(" suscriptores");
}

// -------------------------------------------------------------- PlaylistItem

PlaylistItem::PlaylistItem()
    : local(false)
{
}

// ---------------------------------------------------------------- PlayRecord

PlayRecord::PlayRecord()
    : secondsPlayed(0), music(false)
{
}

PlayRecord PlayRecord::fromVideo(const VideoItem &video, bool music)
{
    PlayRecord record;
    record.videoId = video.id;
    record.title = video.title;
    record.artist = video.channelName;
    record.startedAt = QDateTime::currentDateTimeUtc();
    record.music = music;
    return record;
}

// ----------------------------------------------------------------- LyricLine

LyricLine::LyricLine()
    : time(-1.0)
{
}

// Kept out of the header so the helper stays private to this file but is
// still reachable by anything that includes media_types.h through a call.
QString sightlineFormatCount(qint64 value)
{
    return spanishThousands(value);
}
