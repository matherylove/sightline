#ifndef SIGHTLINE_LIBRARY_H
#define SIGHTLINE_LIBRARY_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QString>

#include "media_types.h"
#include "sightline_paths.h"

class ThumbnailFetcher;

// The whole point of the app: subscriptions, playlists and history live in a
// JSON file on this disk, not in an account. Linking a Google account only
// ever copies data in; nothing is written back and nothing is required.
class Library : public QObject
{
    Q_OBJECT

public:
    explicit Library(const SightlinePaths &paths, QObject *parent = 0);

    bool load(QString *error = 0);
    bool save(QString *error = 0) const;

    // Channels
    QList<ChannelItem> channels() const { return channels_; }
    bool isSubscribed(const QString &channelId) const;
    void subscribe(const ChannelItem &channel);
    void unsubscribe(const QString &channelId);
    ChannelItem channel(const QString &channelId) const;
    void setUnwatched(const QString &channelId, int count);
    int totalUnwatched() const;

    // Playlists
    QList<PlaylistItem> playlists() const { return playlists_; }
    PlaylistItem playlist(const QString &playlistId) const;
    void upsertPlaylist(const PlaylistItem &playlist);
    void removePlaylist(const QString &playlistId);
    void addToPlaylist(const QString &playlistId, const QString &videoId);
    void removeFromPlaylist(const QString &playlistId, const QString &videoId);
    bool playlistContains(const QString &playlistId, const QString &videoId) const;

    // The two lists every install starts with, so "Ver más tarde" and
    // "Me gusta" exist before anything is imported.
    static QString watchLaterId();
    static QString likedId();

    // Video metadata cache. Grids show titles and durations for videos the
    // user has not opened, and re-extracting each one would cost a process.
    void remember(const VideoItem &video);
    VideoItem remembered(const QString &videoId) const;
    bool hasRemembered(const QString &videoId) const;

    // Watch history and resume positions.
    QList<VideoItem> history(int limit = 100) const;
    void recordWatch(const VideoItem &video, qint64 positionSeconds);
    qint64 resumePosition(const QString &videoId) const;
    void clearHistory();

    // Feed: everything from subscribed channels, newest first.
    QList<VideoItem> subscriptionFeed(int limit = 60) const;
    void mergeChannelVideos(const QString &channelId, const QList<VideoItem> &videos);

    // Thumbnails, fetched once and kept as JPEG on disk. Returns a null
    // pixmap and starts a download when the file is not there yet.
    QPixmap thumbnail(const VideoItem &video);
    QPixmap thumbnailIfPresent(const QString &videoId) const;

    // Channel avatars share the fetcher and the cache directory; the id is
    // prefixed so it cannot collide with a video of the same name.
    QPixmap channelAvatar(const ChannelItem &channel);
    void rememberChannelAvatar(const QString &channelId, const QString &url);

    int importSubscriptionsCsv(const QString &path, QString *error = 0);

signals:
    void changed();
    void thumbnailReady(const QString &videoId);

private slots:
    void onThumbnailFetched(const QString &videoId, const QString &filePath);

private:
    QString thumbnailFile(const QString &videoId) const;
    void ensureDefaultPlaylists();

    SightlinePaths paths_;
    QList<ChannelItem> channels_;
    QList<PlaylistItem> playlists_;
    QHash<QString, VideoItem> videos_;
    QList<QString> historyOrder_;
    QHash<QString, qint64> resume_;
    QHash<QString, QStringList> channelVideos_;

    ThumbnailFetcher *fetcher_;
    QHash<QString, QPixmap> pixmapCache_;
};

#endif
