#ifndef SIGHTLINE_LISTENING_STATS_H
#define SIGHTLINE_LISTENING_STATS_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include "media_types.h"
#include "sightline_paths.h"

// The recap screen is a live aggregation of the play log, which is why it
// can be opened at any moment instead of once a year. Nothing here leaves
// the machine; the file is a plain JSON array under library/.
class ListeningStats : public QObject
{
    Q_OBJECT

public:
    enum Period {
        Today = 0,
        Last30Days = 1,
        ThisYear = 2,
        AllTime = 3
    };

    struct Entry
    {
        Entry() : seconds(0), plays(0) {}
        QString key;
        QString label;
        QString sublabel;
        qint64 seconds;
        int plays;
    };

    struct Summary
    {
        Summary()
            : totalSeconds(0), totalPlays(0), distinctTracks(0), distinctArtists(0),
              newArtists(0), streakDays(0), bestStreakDays(0), todaySeconds(0),
              bestDaySeconds(0), busiestHour(-1), weekAverageSeconds(0), weekBestSeconds(0),
              topTrackPlaysInADay(0) {}

        qint64 totalSeconds;
        int totalPlays;
        int distinctTracks;
        int distinctArtists;
        int newArtists;
        int streakDays;
        int bestStreakDays;
        qint64 todaySeconds;
        qint64 bestDaySeconds;
        QDate bestDay;
        int busiestHour;
        qint64 weekAverageSeconds;
        qint64 weekBestSeconds;

        QVector<qint64> hourHistogram;   // 24 entries, seconds
        QVector<qint64> weekHistogram;   // 12 entries, seconds, oldest first
        QList<Entry> topArtists;
        QList<Entry> topTracks;
        QList<Entry> topAlbums;

        QString topTrackTitle;
        QString topTrackArtist;
        int topTrackPlaysInADay;
    };

    explicit ListeningStats(const SightlinePaths &paths, QObject *parent = 0);

    bool load(QString *error = 0);
    bool save(QString *error = 0) const;

    // Called by the player: one open record per track, closed when it stops
    // or the track changes. Anything under fifteen seconds is discarded, so
    // skipping through an album does not distort the figures.
    void beginPlay(const VideoItem &video, bool music);
    void addPlayedSeconds(int seconds);
    void endPlay();

    bool isPlaying() const { return open_.startedAt.isValid(); }
    QString currentTitle() const { return open_.title; }
    int sessionSeconds() const { return sessionSeconds_; }

    Summary summarise(Period period, bool musicOnly = true) const;

    int recordCount() const { return records_.size(); }
    QDateTime firstRecordAt() const;
    qint64 approximateFileSize() const;

    static QString periodLabel(Period period);

signals:
    void updated();

private:
    void flushOpen();
    static QDateTime periodStart(Period period);

    SightlinePaths paths_;
    QList<PlayRecord> records_;
    PlayRecord open_;
    int sessionSeconds_;
    int unsavedCount_;
};

#endif
