#include "library.h"

#include <QDateTime>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "thumbnail_fetcher.h"
#include <QSaveFile>
#include <QTextStream>

namespace {

QString str(const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toString(); }
qint64 num(const QJsonObject &o, const char *k) { return qint64(o.value(QLatin1String(k)).toDouble(0)); }

QDateTime dt(const QJsonObject &o, const char *k)
{
    const QString value = o.value(QLatin1String(k)).toString();
    if (value.isEmpty())
        return QDateTime();
    QDateTime parsed = QDateTime::fromString(value, Qt::ISODate);
    parsed.setTimeSpec(Qt::UTC);
    return parsed;
}

QString dtStr(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODate) : QString();
}

QJsonObject videoToJson(const VideoItem &v)
{
    QJsonObject o;
    o.insert(QString::fromLatin1("id"), v.id);
    o.insert(QString::fromLatin1("title"), v.title);
    o.insert(QString::fromLatin1("channelId"), v.channelId);
    o.insert(QString::fromLatin1("channelName"), v.channelName);
    o.insert(QString::fromLatin1("duration"), double(v.duration));
    o.insert(QString::fromLatin1("viewCount"), double(v.viewCount));
    o.insert(QString::fromLatin1("published"), dtStr(v.published));
    o.insert(QString::fromLatin1("thumbnailUrl"), v.thumbnailUrl);
    return o;
}

VideoItem videoFromJson(const QJsonObject &o)
{
    VideoItem v;
    v.id = str(o, "id");
    v.title = str(o, "title");
    v.channelId = str(o, "channelId");
    v.channelName = str(o, "channelName");
    v.duration = num(o, "duration");
    v.viewCount = num(o, "viewCount");
    v.published = dt(o, "published");
    v.thumbnailUrl = str(o, "thumbnailUrl");
    return v;
}

} // namespace

Library::Library(const SightlinePaths &paths, QObject *parent)
    : QObject(parent), paths_(paths), fetcher_(0)
{
    fetcher_ = new ThumbnailFetcher(paths_.thumbnails(), this);
    connect(fetcher_, SIGNAL(fetched(QString, QString)),
            this, SLOT(onThumbnailFetched(QString, QString)), Qt::QueuedConnection);
    fetcher_->start(QThread::LowPriority);
}

QString Library::watchLaterId() { return QString::fromLatin1("local:watchlater"); }
QString Library::likedId()      { return QString::fromLatin1("local:liked"); }

void Library::ensureDefaultPlaylists()
{
    bool hasWatchLater = false;
    bool hasLiked = false;
    for (int i = 0; i < playlists_.size(); ++i) {
        if (playlists_.at(i).id == watchLaterId()) hasWatchLater = true;
        if (playlists_.at(i).id == likedId()) hasLiked = true;
    }
    if (!hasWatchLater) {
        PlaylistItem list;
        list.id = watchLaterId();
        list.title = QString::fromUtf8("Ver más tarde");
        list.local = true;
        list.updatedAt = QDateTime::currentDateTimeUtc();
        playlists_.prepend(list);
    }
    if (!hasLiked) {
        PlaylistItem list;
        list.id = likedId();
        list.title = QString::fromUtf8("Me gusta");
        list.local = true;
        list.updatedAt = QDateTime::currentDateTimeUtc();
        playlists_.insert(hasWatchLater ? 0 : 1, list);
    }
}

bool Library::load(QString *error)
{
    channels_.clear();
    playlists_.clear();
    videos_.clear();
    historyOrder_.clear();
    resume_.clear();
    channelVideos_.clear();

    QFile file(paths_.libraryFile());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

        const QJsonArray channels = root.value(QString::fromLatin1("channels")).toArray();
        for (int i = 0; i < channels.size(); ++i) {
            const QJsonObject o = channels.at(i).toObject();
            ChannelItem channel;
            channel.id = str(o, "id");
            channel.name = str(o, "name");
            channel.handle = str(o, "handle");
            channel.avatarUrl = str(o, "avatarUrl");
            channel.subscriberCount = num(o, "subscriberCount");
            channel.subscribedAt = dt(o, "subscribedAt");
            channel.lastCheckedAt = dt(o, "lastCheckedAt");
            channel.unwatchedCount = int(num(o, "unwatchedCount"));
            if (!channel.id.isEmpty())
                channels_.append(channel);
        }

        const QJsonArray playlists = root.value(QString::fromLatin1("playlists")).toArray();
        for (int i = 0; i < playlists.size(); ++i) {
            const QJsonObject o = playlists.at(i).toObject();
            PlaylistItem list;
            list.id = str(o, "id");
            list.title = str(o, "title");
            list.ownerName = str(o, "ownerName");
            list.local = o.value(QString::fromLatin1("local")).toBool(false);
            list.updatedAt = dt(o, "updatedAt");
            const QJsonArray ids = o.value(QString::fromLatin1("videoIds")).toArray();
            for (int j = 0; j < ids.size(); ++j)
                list.videoIds.append(ids.at(j).toString());
            if (!list.id.isEmpty())
                playlists_.append(list);
        }

        const QJsonArray videos = root.value(QString::fromLatin1("videos")).toArray();
        for (int i = 0; i < videos.size(); ++i) {
            const VideoItem video = videoFromJson(videos.at(i).toObject());
            if (!video.id.isEmpty())
                videos_.insert(video.id, video);
        }

        const QJsonObject feeds = root.value(QString::fromLatin1("channelVideos")).toObject();
        for (QJsonObject::const_iterator it = feeds.constBegin(); it != feeds.constEnd(); ++it) {
            QStringList ids;
            const QJsonArray array = it.value().toArray();
            for (int j = 0; j < array.size(); ++j)
                ids.append(array.at(j).toString());
            channelVideos_.insert(it.key(), ids);
        }
    }

    QFile historyFile(paths_.historyFile());
    if (historyFile.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(historyFile.readAll()).object();
        const QJsonArray entries = root.value(QString::fromLatin1("entries")).toArray();
        for (int i = 0; i < entries.size(); ++i) {
            const QJsonObject o = entries.at(i).toObject();
            const QString id = str(o, "videoId");
            if (id.isEmpty())
                continue;
            historyOrder_.append(id);
            resume_.insert(id, num(o, "position"));
        }
    }

    ensureDefaultPlaylists();
    Q_UNUSED(error);
    return true;
}

bool Library::save(QString *error) const
{
    QJsonArray channels;
    for (int i = 0; i < channels_.size(); ++i) {
        const ChannelItem &c = channels_.at(i);
        QJsonObject o;
        o.insert(QString::fromLatin1("id"), c.id);
        o.insert(QString::fromLatin1("name"), c.name);
        o.insert(QString::fromLatin1("handle"), c.handle);
        o.insert(QString::fromLatin1("avatarUrl"), c.avatarUrl);
        o.insert(QString::fromLatin1("subscriberCount"), double(c.subscriberCount));
        o.insert(QString::fromLatin1("subscribedAt"), dtStr(c.subscribedAt));
        o.insert(QString::fromLatin1("lastCheckedAt"), dtStr(c.lastCheckedAt));
        o.insert(QString::fromLatin1("unwatchedCount"), c.unwatchedCount);
        channels.append(o);
    }

    QJsonArray playlists;
    for (int i = 0; i < playlists_.size(); ++i) {
        const PlaylistItem &p = playlists_.at(i);
        QJsonObject o;
        o.insert(QString::fromLatin1("id"), p.id);
        o.insert(QString::fromLatin1("title"), p.title);
        o.insert(QString::fromLatin1("ownerName"), p.ownerName);
        o.insert(QString::fromLatin1("local"), p.local);
        o.insert(QString::fromLatin1("updatedAt"), dtStr(p.updatedAt));
        QJsonArray ids;
        for (int j = 0; j < p.videoIds.size(); ++j)
            ids.append(p.videoIds.at(j));
        o.insert(QString::fromLatin1("videoIds"), ids);
        playlists.append(o);
    }

    // Only videos that something still points at are written out, or the
    // file grows without bound as the user browses.
    QJsonArray videos;
    for (QHash<QString, VideoItem>::const_iterator it = videos_.constBegin();
         it != videos_.constEnd(); ++it) {
        videos.append(videoToJson(it.value()));
    }

    QJsonObject feeds;
    for (QHash<QString, QStringList>::const_iterator it = channelVideos_.constBegin();
         it != channelVideos_.constEnd(); ++it) {
        QJsonArray ids;
        for (int j = 0; j < it.value().size(); ++j)
            ids.append(it.value().at(j));
        feeds.insert(it.key(), ids);
    }

    QJsonObject root;
    root.insert(QString::fromLatin1("formatVersion"), 1);
    root.insert(QString::fromLatin1("channels"), channels);
    root.insert(QString::fromLatin1("playlists"), playlists);
    root.insert(QString::fromLatin1("videos"), videos);
    root.insert(QString::fromLatin1("channelVideos"), feeds);

    QSaveFile file(paths_.libraryFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QString::fromUtf8("No se pudo escribir la biblioteca.");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) *error = QString::fromUtf8("No se pudo guardar la biblioteca.");
        return false;
    }

    QJsonArray entries;
    for (int i = 0; i < historyOrder_.size(); ++i) {
        const QString id = historyOrder_.at(i);
        QJsonObject o;
        o.insert(QString::fromLatin1("videoId"), id);
        o.insert(QString::fromLatin1("position"), double(resume_.value(id, 0)));
        entries.append(o);
    }
    QJsonObject historyRoot;
    historyRoot.insert(QString::fromLatin1("entries"), entries);

    QSaveFile historyFile(paths_.historyFile());
    if (historyFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        historyFile.write(QJsonDocument(historyRoot).toJson(QJsonDocument::Compact));
        historyFile.commit();
    }
    return true;
}

bool Library::isSubscribed(const QString &channelId) const
{
    for (int i = 0; i < channels_.size(); ++i)
        if (channels_.at(i).id == channelId)
            return true;
    return false;
}

void Library::subscribe(const ChannelItem &channel)
{
    if (channel.id.isEmpty())
        return;
    for (int i = 0; i < channels_.size(); ++i) {
        if (channels_.at(i).id == channel.id) {
            ChannelItem merged = channel;
            merged.subscribedAt = channels_.at(i).subscribedAt;
            merged.unwatchedCount = channels_.at(i).unwatchedCount;
            channels_[i] = merged;
            emit changed();
            return;
        }
    }
    ChannelItem added = channel;
    if (!added.subscribedAt.isValid())
        added.subscribedAt = QDateTime::currentDateTimeUtc();
    channels_.append(added);
    emit changed();
}

void Library::unsubscribe(const QString &channelId)
{
    for (int i = 0; i < channels_.size(); ++i) {
        if (channels_.at(i).id == channelId) {
            channels_.removeAt(i);
            emit changed();
            return;
        }
    }
}

ChannelItem Library::channel(const QString &channelId) const
{
    for (int i = 0; i < channels_.size(); ++i)
        if (channels_.at(i).id == channelId)
            return channels_.at(i);
    return ChannelItem();
}

void Library::setUnwatched(const QString &channelId, int count)
{
    for (int i = 0; i < channels_.size(); ++i) {
        if (channels_.at(i).id == channelId) {
            channels_[i].unwatchedCount = count;
            emit changed();
            return;
        }
    }
}

int Library::totalUnwatched() const
{
    int total = 0;
    for (int i = 0; i < channels_.size(); ++i)
        total += channels_.at(i).unwatchedCount;
    return total;
}

PlaylistItem Library::playlist(const QString &playlistId) const
{
    for (int i = 0; i < playlists_.size(); ++i)
        if (playlists_.at(i).id == playlistId)
            return playlists_.at(i);
    return PlaylistItem();
}

void Library::upsertPlaylist(const PlaylistItem &playlist)
{
    if (playlist.id.isEmpty())
        return;
    for (int i = 0; i < playlists_.size(); ++i) {
        if (playlists_.at(i).id == playlist.id) {
            playlists_[i] = playlist;
            emit changed();
            return;
        }
    }
    playlists_.append(playlist);
    emit changed();
}

void Library::removePlaylist(const QString &playlistId)
{
    // The two built-in lists are part of the shell, not user data; removing
    // them would leave the sidebar with dead entries after the next load.
    if (playlistId == watchLaterId() || playlistId == likedId())
        return;
    for (int i = 0; i < playlists_.size(); ++i) {
        if (playlists_.at(i).id == playlistId) {
            playlists_.removeAt(i);
            emit changed();
            return;
        }
    }
}

void Library::addToPlaylist(const QString &playlistId, const QString &videoId)
{
    for (int i = 0; i < playlists_.size(); ++i) {
        if (playlists_.at(i).id != playlistId)
            continue;
        if (!playlists_.at(i).videoIds.contains(videoId)) {
            playlists_[i].videoIds.prepend(videoId);
            playlists_[i].updatedAt = QDateTime::currentDateTimeUtc();
            emit changed();
        }
        return;
    }
}

void Library::removeFromPlaylist(const QString &playlistId, const QString &videoId)
{
    for (int i = 0; i < playlists_.size(); ++i) {
        if (playlists_.at(i).id != playlistId)
            continue;
        if (playlists_[i].videoIds.removeAll(videoId) > 0) {
            playlists_[i].updatedAt = QDateTime::currentDateTimeUtc();
            emit changed();
        }
        return;
    }
}

bool Library::playlistContains(const QString &playlistId, const QString &videoId) const
{
    return playlist(playlistId).videoIds.contains(videoId);
}

void Library::remember(const VideoItem &video)
{
    if (video.id.isEmpty())
        return;

    // A flat listing carries no duration or view count. Merging rather than
    // replacing keeps whatever a previous full extraction already told us.
    VideoItem stored = videos_.value(video.id);
    if (stored.id.isEmpty())
        stored = video;
    else {
        if (!video.title.isEmpty())        stored.title = video.title;
        if (!video.channelId.isEmpty())    stored.channelId = video.channelId;
        if (!video.channelName.isEmpty())  stored.channelName = video.channelName;
        if (video.duration > 0)            stored.duration = video.duration;
        if (video.viewCount > 0)           stored.viewCount = video.viewCount;
        if (video.likeCount > 0)           stored.likeCount = video.likeCount;
        if (video.published.isValid())     stored.published = video.published;
        if (!video.thumbnailUrl.isEmpty()) stored.thumbnailUrl = video.thumbnailUrl;
        if (!video.description.isEmpty())  stored.description = video.description;
        if (!video.formats.isEmpty()) {
            stored.formats = video.formats;
            stored.chapters = video.chapters;
            stored.detailed = true;
            stored.urlsFetchedAt = video.urlsFetchedAt;
        }
    }
    stored.resumePosition = resume_.value(video.id, stored.resumePosition);
    videos_.insert(stored.id, stored);
}

VideoItem Library::remembered(const QString &videoId) const
{
    VideoItem video = videos_.value(videoId);
    if (!video.id.isEmpty())
        video.resumePosition = resume_.value(videoId, 0);
    return video;
}

bool Library::hasRemembered(const QString &videoId) const
{
    return videos_.contains(videoId);
}

QList<VideoItem> Library::history(int limit) const
{
    QList<VideoItem> result;
    for (int i = 0; i < historyOrder_.size() && result.size() < limit; ++i) {
        const VideoItem video = remembered(historyOrder_.at(i));
        if (!video.id.isEmpty())
            result.append(video);
    }
    return result;
}

void Library::recordWatch(const VideoItem &video, qint64 positionSeconds)
{
    if (video.id.isEmpty())
        return;
    remember(video);
    historyOrder_.removeAll(video.id);
    historyOrder_.prepend(video.id);
    while (historyOrder_.size() > 500)
        historyOrder_.removeLast();

    // Treat the last few seconds as finished: resuming a video two seconds
    // from the end is never what anyone wanted.
    if (video.duration > 0 && positionSeconds > video.duration - 10)
        resume_.remove(video.id);
    else if (positionSeconds > 5)
        resume_.insert(video.id, positionSeconds);

    emit changed();
}

qint64 Library::resumePosition(const QString &videoId) const
{
    return resume_.value(videoId, 0);
}

void Library::clearHistory()
{
    historyOrder_.clear();
    resume_.clear();
    emit changed();
}

void Library::mergeChannelVideos(const QString &channelId, const QList<VideoItem> &videos)
{
    QStringList ids = channelVideos_.value(channelId);
    for (int i = videos.size() - 1; i >= 0; --i) {
        const VideoItem &video = videos.at(i);
        if (video.id.isEmpty())
            continue;
        VideoItem stamped = video;
        if (stamped.channelId.isEmpty())
            stamped.channelId = channelId;
        remember(stamped);
        ids.removeAll(video.id);
        ids.prepend(video.id);
    }
    while (ids.size() > 60)
        ids.removeLast();
    channelVideos_.insert(channelId, ids);

    for (int i = 0; i < channels_.size(); ++i) {
        if (channels_.at(i).id == channelId) {
            channels_[i].lastCheckedAt = QDateTime::currentDateTimeUtc();
            int unwatched = 0;
            for (int j = 0; j < ids.size(); ++j)
                if (!historyOrder_.contains(ids.at(j)))
                    ++unwatched;
            channels_[i].unwatchedCount = qMin(unwatched, 99);
            break;
        }
    }
    emit changed();
}

QList<VideoItem> Library::subscriptionFeed(int limit) const
{
    QList<VideoItem> feed;
    for (int i = 0; i < channels_.size(); ++i) {
        const QStringList ids = channelVideos_.value(channels_.at(i).id);
        for (int j = 0; j < ids.size(); ++j) {
            const VideoItem video = remembered(ids.at(j));
            if (!video.id.isEmpty())
                feed.append(video);
        }
    }

    // Newest first. An insertion sort is fine here: the list is at most a
    // few hundred entries and this runs once per view change.
    for (int i = 1; i < feed.size(); ++i) {
        const VideoItem key = feed.at(i);
        int j = i - 1;
        while (j >= 0 && feed.at(j).published < key.published) {
            feed[j + 1] = feed.at(j);
            --j;
        }
        feed[j + 1] = key;
    }

    while (feed.size() > limit)
        feed.removeLast();
    return feed;
}

QString Library::thumbnailFile(const QString &videoId) const
{
    return paths_.thumbnails() + QString::fromLatin1("/") + videoId + QString::fromLatin1(".jpg");
}

QPixmap Library::thumbnailIfPresent(const QString &videoId) const
{
    return pixmapCache_.value(videoId);
}

QPixmap Library::thumbnail(const VideoItem &video)
{
    if (video.id.isEmpty())
        return QPixmap();

    if (pixmapCache_.contains(video.id))
        return pixmapCache_.value(video.id);

    const QString path = thumbnailFile(video.id);
    if (QFileInfo(path).isFile()) {
        QPixmap pixmap;
        if (pixmap.load(path)) {
            pixmapCache_.insert(video.id, pixmap);
            return pixmap;
        }
    }

    // Queued on the worker thread. The grid draws its placeholder tile until
    // thumbnailReady arrives, so nothing blocks here.
    fetcher_->request(video.id, video.thumbnailUrl);
    return QPixmap();
}

void Library::onThumbnailFetched(const QString &videoId, const QString &filePath)
{
    QPixmap pixmap;
    if (!pixmap.load(filePath))
        return;
    pixmapCache_.insert(videoId, pixmap);
    emit thumbnailReady(videoId);
}

int Library::importSubscriptionsCsv(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString::fromUtf8("No se pudo abrir el archivo.");
        return 0;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");

    int imported = 0;
    bool first = true;
    while (!stream.atEnd()) {
        const QString rawLine = stream.readLine();
        if (first) {
            first = false;
            // Takeout writes a header row; skip it without assuming the
            // exact wording, which changes with the interface language.
            if (rawLine.contains(QLatin1String("Channel"), Qt::CaseInsensitive)
                || rawLine.contains(QLatin1String("Canal"), Qt::CaseInsensitive))
                continue;
        }
        if (rawLine.trimmed().isEmpty())
            continue;

        // Takeout's format is: channel id, channel url, channel title.
        // Titles can contain commas, so the split is limited to two.
        const int firstComma = rawLine.indexOf(QLatin1Char(','));
        if (firstComma < 0)
            continue;
        const int secondComma = rawLine.indexOf(QLatin1Char(','), firstComma + 1);
        if (secondComma < 0)
            continue;

        ChannelItem channel;
        channel.id = rawLine.left(firstComma).trimmed();
        channel.name = rawLine.mid(secondComma + 1).trimmed();
        if (channel.name.startsWith(QLatin1Char('"')) && channel.name.endsWith(QLatin1Char('"')))
            channel.name = channel.name.mid(1, channel.name.size() - 2);
        if (channel.id.isEmpty() || !channel.id.startsWith(QLatin1String("UC")))
            continue;

        channel.subscribedAt = QDateTime::currentDateTimeUtc();
        if (!isSubscribed(channel.id)) {
            channels_.append(channel);
            ++imported;
        }
    }

    if (imported > 0)
        emit changed();
    return imported;
}
