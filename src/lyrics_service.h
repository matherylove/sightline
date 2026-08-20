#ifndef SIGHTLINE_LYRICS_SERVICE_H
#define SIGHTLINE_LYRICS_SERVICE_H

#include <QList>
#include <QObject>
#include <QString>

#include "media_types.h"
#include "sightline_paths.h"

class QThread;
class LyricsWorker;

// Fetches synchronised lyrics from LRCLIB.
//
// LRCLIB is chosen because it needs no key, no account and no attribution
// header, and it answers with plain LRC. The panel existed since the mockup
// but nothing ever filled it, which is why every track reported "sin letra".
//
// Results are cached as .lrc beside the rest of the cache, so the file is
// also usable outside Sightline, and a track already looked up costs nothing.
class LyricsService : public QObject
{
    Q_OBJECT

public:
    explicit LyricsService(const SightlinePaths &paths, QObject *parent = 0);
    ~LyricsService();

    // Titles from YouTube Music carry a lot that LRCLIB will not match on:
    // "(Official Video)", "[4K Remaster]", "- Topic". Stripping them is the
    // difference between finding a track and not.
    static QString cleanTitle(const QString &title);
    static QString cleanArtist(const QString &artist);

    void request(const VideoItem &video);
    void cancel();

signals:
    void lyricsReady(const QString &videoId, const QList<LyricLine> &lines, bool synced);
    void lyricsMissing(const QString &videoId);

public slots:
    // Invoked from the worker thread through a queued call.
    void onFetched();

private:

    QString cacheFile(const QString &videoId) const;
    bool loadFromCache(const QString &videoId, QList<LyricLine> *lines, bool *synced) const;
    static QList<LyricLine> parseLrc(const QString &text, bool *synced);

    SightlinePaths paths_;
    LyricsWorker *worker_;
};

#endif
