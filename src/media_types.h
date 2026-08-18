#ifndef SIGHTLINE_MEDIA_TYPES_H
#define SIGHTLINE_MEDIA_TYPES_H

#include <QDateTime>
#include <QMetaType>
#include <QList>
#include <QString>
#include <QStringList>

// The vocabulary shared by the extractor, the library and every widget.
// These mirror the fields yt-dlp actually emits in its JSON dump, so nothing
// here needs translating twice.

struct MediaFormat
{
    MediaFormat();

    QString itag;            // "137", "140", "18"
    QString url;
    QString extension;       // "mp4", "m4a", "webm"
    QString videoCodec;      // "avc1.640028", "none"
    QString audioCodec;      // "mp4a.40.2", "opus", "none"
    int width;
    int height;
    double fps;
    qint64 bitrate;          // bits per second, 0 when unknown
    qint64 fileSize;         // bytes, 0 when unknown
    QString note;            // "1080p", "medium"

    bool hasVideo() const;
    bool hasAudio() const;

    // True when the video codec is H.264. Everything else costs too much CPU
    // on the machines this targets, so the format lists grey them out rather
    // than hiding them: a user who knows their box can handle VP9 can switch.
    bool isAvc() const;

    QString qualityLabel() const;   // "1080p", "AAC 128 kb/s"
    QString bitrateLabel() const;   // "4,1 Mb/s"
    QString sizeLabel() const;      // "248 MB"
};

struct SponsorSegment
{
    enum Category {
        Sponsor = 0,
        Intro = 1,
        SelfPromo = 2,
        Interaction = 3,
        MusicOffTopic = 4,
        Preview = 5,
        Filler = 6,
        UnknownCategory = 7
    };

    SponsorSegment();

    Category category;
    double start;            // seconds
    double end;              // seconds
    QString uuid;
    bool skipped;            // set once this run has jumped over it

    double duration() const { return end - start; }

    static Category categoryFromApi(const QString &name);
    static QString apiName(Category category);
    static QString displayName(Category category);
    static QString description(Category category);
};

struct VideoComment
{
    VideoComment();

    QString id;
    QString parentId;        // "root" for top level
    QString author;
    QString text;
    int likeCount;
    int replyCount;
    bool authorIsUploader;
    bool pinned;
    QDateTime published;

    bool isReply() const { return !parentId.isEmpty() && parentId != QLatin1String("root"); }
};

struct VideoChapter
{
    VideoChapter();

    QString title;
    double start;
    double end;
};

// A video as it appears in a grid, a recommendation list or the history.
// Populated from a flat playlist entry, which carries no formats, or from a
// full extraction, which carries everything.
struct VideoItem
{
    VideoItem();

    QString id;
    QString title;
    QString channelId;
    QString channelName;
    QString description;
    qint64 duration;         // seconds
    qint64 viewCount;
    qint64 likeCount;
    QDateTime published;
    QString thumbnailUrl;

    QList<MediaFormat> formats;
    QList<VideoChapter> chapters;

    // What yt-dlp's own selector picked, and the SponsorBlock segments it
    // returned with the extraction. Both come free with the same process
    // call, so nothing here is resolved twice.
    QList<MediaFormat> requested;
    QList<SponsorSegment> segments;

    bool detailed;           // false when it came from a flat listing
    qint64 resumePosition;   // seconds, 0 when unwatched
    QDateTime urlsFetchedAt; // googlevideo URLs expire; this dates them

    QString watchUrl() const;
    QString durationLabel() const;      // "18:24", "1:04:32"
    QString viewCountLabel() const;     // "41 mil"
    QString publishedLabel() const;     // "hace 2 h"

    // The googlevideo links are handed out with roughly a six hour life and
    // are tied to the address that asked for them. Anything older is refused
    // with a 403, so the player re-extracts instead of failing.
    bool urlsLikelyExpired() const;

    // Prefer what yt-dlp resolved; fall back to walking the list only when
    // the selector produced nothing usable.
    const MediaFormat *selectedVideo() const;
    const MediaFormat *selectedAudio() const;

    const MediaFormat *bestVideoFormat(int maxHeight, bool avcOnly) const;
    const MediaFormat *bestAudioFormat(bool preferAac) const;
    const MediaFormat *formatByItag(const QString &itag) const;
};

struct ChannelItem
{
    ChannelItem();

    QString id;
    QString name;
    QString handle;
    QString avatarUrl;
    qint64 subscriberCount;
    QDateTime subscribedAt;
    QDateTime lastCheckedAt;
    int unwatchedCount;

    QString url() const;
    QString subscriberLabel() const;
};

struct PlaylistItem
{
    PlaylistItem();

    QString id;              // remote id, or a local "local:xxxx"
    QString title;
    QString ownerName;
    QStringList videoIds;
    bool local;              // built here, never synced
    QDateTime updatedAt;

    bool isLocal() const { return local || id.startsWith(QLatin1String("local:")); }
};

// One entry in the listening log. The recap screen is a live aggregation of
// these, which is why it can be shown at any moment instead of once a year.
struct PlayRecord
{
    PlayRecord();

    QString videoId;
    QString title;
    QString artist;
    QString album;
    QDateTime startedAt;
    int secondsPlayed;
    bool music;

    static PlayRecord fromVideo(const VideoItem &video, bool music);
};

struct LyricLine
{
    LyricLine();

    double time;             // seconds, -1 when the lyric is unsynced
    QString text;

    bool synced() const { return time >= 0.0; }
};

Q_DECLARE_METATYPE(VideoItem)
Q_DECLARE_METATYPE(ChannelItem)
Q_DECLARE_METATYPE(PlaylistItem)
Q_DECLARE_METATYPE(VideoComment)
Q_DECLARE_METATYPE(SponsorSegment)

#endif
