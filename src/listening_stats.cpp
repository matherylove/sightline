#include "listening_stats.h"

#include <QDate>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace {

// Sorts entries by seconds, descending. A selection sort is deliberate: the
// candidate list is small and this avoids dragging in <algorithm> comparators
// that MSVC 2017 in C++11 mode handles inconsistently with Qt containers.
void sortBySeconds(QList<ListeningStats::Entry> &entries)
{
    for (int i = 0; i < entries.size(); ++i) {
        int best = i;
        for (int j = i + 1; j < entries.size(); ++j)
            if (entries.at(j).seconds > entries.at(best).seconds)
                best = j;
        if (best != i)
            entries.swap(i, best);
    }
}

void sortByPlays(QList<ListeningStats::Entry> &entries)
{
    for (int i = 0; i < entries.size(); ++i) {
        int best = i;
        for (int j = i + 1; j < entries.size(); ++j)
            if (entries.at(j).plays > entries.at(best).plays)
                best = j;
        if (best != i)
            entries.swap(i, best);
    }
}

} // namespace

ListeningStats::ListeningStats(const SightlinePaths &paths, QObject *parent)
    : QObject(parent), paths_(paths), sessionSeconds_(0), unsavedCount_(0)
{
}

bool ListeningStats::load(QString *error)
{
    records_.clear();

    QFile file(paths_.playLogFile());
    if (!file.open(QIODevice::ReadOnly)) {
        Q_UNUSED(error);
        return true;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray entries = root.value(QString::fromLatin1("plays")).toArray();
    for (int i = 0; i < entries.size(); ++i) {
        const QJsonObject o = entries.at(i).toObject();
        PlayRecord record;
        record.videoId = o.value(QString::fromLatin1("videoId")).toString();
        record.title = o.value(QString::fromLatin1("title")).toString();
        record.artist = o.value(QString::fromLatin1("artist")).toString();
        record.album = o.value(QString::fromLatin1("album")).toString();
        record.secondsPlayed = o.value(QString::fromLatin1("seconds")).toInt(0);
        record.music = o.value(QString::fromLatin1("music")).toBool(true);
        record.startedAt = QDateTime::fromString(
            o.value(QString::fromLatin1("startedAt")).toString(), Qt::ISODate);
        record.startedAt.setTimeSpec(Qt::UTC);
        if (record.startedAt.isValid() && record.secondsPlayed > 0)
            records_.append(record);
    }
    return true;
}

bool ListeningStats::save(QString *error) const
{
    QJsonArray entries;
    for (int i = 0; i < records_.size(); ++i) {
        const PlayRecord &record = records_.at(i);
        QJsonObject o;
        o.insert(QString::fromLatin1("videoId"), record.videoId);
        o.insert(QString::fromLatin1("title"), record.title);
        o.insert(QString::fromLatin1("artist"), record.artist);
        o.insert(QString::fromLatin1("album"), record.album);
        o.insert(QString::fromLatin1("seconds"), record.secondsPlayed);
        o.insert(QString::fromLatin1("music"), record.music);
        o.insert(QString::fromLatin1("startedAt"), record.startedAt.toUTC().toString(Qt::ISODate));
        entries.append(o);
    }

    QJsonObject root;
    root.insert(QString::fromLatin1("formatVersion"), 1);
    root.insert(QString::fromLatin1("plays"), entries);

    QSaveFile file(paths_.playLogFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QString::fromUtf8("No se pudo escribir el registro de reproducción.");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) *error = QString::fromUtf8("No se pudo guardar el registro de reproducción.");
        return false;
    }
    return true;
}

void ListeningStats::beginPlay(const VideoItem &video, bool music)
{
    flushOpen();
    open_ = PlayRecord::fromVideo(video, music);
    sessionSeconds_ = 0;
}

void ListeningStats::addPlayedSeconds(int seconds)
{
    if (!open_.startedAt.isValid() || seconds <= 0)
        return;
    open_.secondsPlayed += seconds;
    sessionSeconds_ += seconds;
    emit updated();
}

void ListeningStats::endPlay()
{
    flushOpen();
    emit updated();
}

void ListeningStats::flushOpen()
{
    if (!open_.startedAt.isValid()) {
        open_ = PlayRecord();
        return;
    }

    // Fifteen seconds, so that skipping through an album to find a track
    // does not inflate the play counts of everything skipped past.
    if (open_.secondsPlayed >= 15) {
        records_.append(open_);
        if (++unsavedCount_ >= 5) {
            unsavedCount_ = 0;
            save();
        }
    }
    open_ = PlayRecord();
}

QDateTime ListeningStats::periodStart(Period period)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    switch (period) {
    case Today:
        return QDateTime(now.date(), QTime(0, 0), Qt::UTC);
    case Last30Days:
        return now.addDays(-30);
    case ThisYear:
        return QDateTime(QDate(now.date().year(), 1, 1), QTime(0, 0), Qt::UTC);
    default:
        break;
    }
    return QDateTime();
}

QString ListeningStats::periodLabel(Period period)
{
    switch (period) {
    case Today:       return QString::fromUtf8("Hoy");
    case Last30Days:  return QString::fromUtf8("Últimos 30 días");
    case ThisYear:    return QString::fromUtf8("Este año");
    default:          return QString::fromUtf8("Desde siempre");
    }
}

QDateTime ListeningStats::firstRecordAt() const
{
    QDateTime earliest;
    for (int i = 0; i < records_.size(); ++i) {
        if (!earliest.isValid() || records_.at(i).startedAt < earliest)
            earliest = records_.at(i).startedAt;
    }
    return earliest;
}

qint64 ListeningStats::approximateFileSize() const
{
    // Roughly 230 bytes per record once the JSON overhead is counted.
    return qint64(records_.size()) * 230;
}

ListeningStats::Summary ListeningStats::summarise(Period period, bool musicOnly) const
{
    Summary summary;
    summary.hourHistogram.fill(0, 24);
    summary.weekHistogram.fill(0, 12);

    const QDateTime cutoff = periodStart(period);
    const QDate today = QDateTime::currentDateTimeUtc().date();

    QHash<QString, Entry> artists;
    QHash<QString, Entry> tracks;
    QHash<QString, Entry> albums;
    QHash<qint64, qint64> secondsByDay;      // julian day -> seconds
    QHash<QString, int> playsPerTrackPerDay; // "id|julian" -> count
    QSet<QString> seenArtistsInPeriod;
    QSet<QString> seenArtistsBefore;

    // The open record is folded in so the numbers move while a track plays.
    QList<PlayRecord> all = records_;
    if (open_.startedAt.isValid() && open_.secondsPlayed > 0)
        all.append(open_);

    for (int i = 0; i < all.size(); ++i) {
        const PlayRecord &record = all.at(i);
        if (musicOnly && !record.music)
            continue;
        if (!record.startedAt.isValid())
            continue;

        const bool inPeriod = !cutoff.isValid() || record.startedAt >= cutoff;
        if (!inPeriod) {
            if (!record.artist.isEmpty())
                seenArtistsBefore.insert(record.artist);
            continue;
        }

        const QDateTime local = record.startedAt.toLocalTime();
        const QDate date = local.date();

        summary.totalSeconds += record.secondsPlayed;
        summary.totalPlays += 1;
        summary.hourHistogram[local.time().hour()] += record.secondsPlayed;

        if (date == today)
            summary.todaySeconds += record.secondsPlayed;

        const int weeksAgo = int(date.daysTo(today) / 7);
        if (weeksAgo >= 0 && weeksAgo < 12)
            summary.weekHistogram[11 - weeksAgo] += record.secondsPlayed;

        secondsByDay[date.toJulianDay()] += record.secondsPlayed;

        if (!record.artist.isEmpty()) {
            Entry &entry = artists[record.artist];
            entry.key = record.artist;
            entry.label = record.artist;
            entry.seconds += record.secondsPlayed;
            entry.plays += 1;
            seenArtistsInPeriod.insert(record.artist);
        }
        if (!record.videoId.isEmpty()) {
            Entry &entry = tracks[record.videoId];
            entry.key = record.videoId;
            entry.label = record.title;
            entry.sublabel = record.artist;
            entry.seconds += record.secondsPlayed;
            entry.plays += 1;

            const QString dayKey = record.videoId + QLatin1Char('|')
                + QString::number(date.toJulianDay());
            const int count = playsPerTrackPerDay.value(dayKey, 0) + 1;
            playsPerTrackPerDay.insert(dayKey, count);
            if (count > summary.topTrackPlaysInADay) {
                summary.topTrackPlaysInADay = count;
                summary.topTrackTitle = record.title;
                summary.topTrackArtist = record.artist;
            }
        }
        if (!record.album.isEmpty()) {
            Entry &entry = albums[record.album];
            entry.key = record.album;
            entry.label = record.album;
            entry.seconds += record.secondsPlayed;
            entry.plays += 1;
        }
    }

    summary.distinctTracks = tracks.size();
    summary.distinctArtists = artists.size();

    QSet<QString> newOnes = seenArtistsInPeriod;
    newOnes.subtract(seenArtistsBefore);
    summary.newArtists = newOnes.size();

    summary.topArtists = artists.values();
    sortBySeconds(summary.topArtists);
    while (summary.topArtists.size() > 7)
        summary.topArtists.removeLast();

    summary.topTracks = tracks.values();
    sortByPlays(summary.topTracks);
    while (summary.topTracks.size() > 4)
        summary.topTracks.removeLast();

    summary.topAlbums = albums.values();
    sortBySeconds(summary.topAlbums);
    while (summary.topAlbums.size() > 3)
        summary.topAlbums.removeLast();

    for (QHash<qint64, qint64>::const_iterator it = secondsByDay.constBegin();
         it != secondsByDay.constEnd(); ++it) {
        if (it.value() > summary.bestDaySeconds) {
            summary.bestDaySeconds = it.value();
            summary.bestDay = QDate::fromJulianDay(it.key());
        }
    }

    int busiest = -1;
    qint64 busiestSeconds = 0;
    for (int hour = 0; hour < 24; ++hour) {
        if (summary.hourHistogram.at(hour) > busiestSeconds) {
            busiestSeconds = summary.hourHistogram.at(hour);
            busiest = hour;
        }
    }
    summary.busiestHour = busiest;

    qint64 weekTotal = 0;
    int weeksWithPlays = 0;
    for (int i = 0; i < summary.weekHistogram.size(); ++i) {
        const qint64 value = summary.weekHistogram.at(i);
        weekTotal += value;
        if (value > 0)
            ++weeksWithPlays;
        if (value > summary.weekBestSeconds)
            summary.weekBestSeconds = value;
    }
    summary.weekAverageSeconds = weeksWithPlays > 0 ? weekTotal / weeksWithPlays : 0;

    // The streak walks backwards from today across every record ever kept,
    // not just this period, because a 23 day streak does not restart when
    // the user switches the view to "today".
    QSet<qint64> daysWithPlays;
    for (int i = 0; i < all.size(); ++i) {
        if (musicOnly && !all.at(i).music)
            continue;
        if (all.at(i).startedAt.isValid())
            daysWithPlays.insert(all.at(i).startedAt.toLocalTime().date().toJulianDay());
    }

    qint64 cursor = today.toJulianDay();
    if (!daysWithPlays.contains(cursor))
        cursor -= 1;    // a day that has not started counting yet must not break it
    while (daysWithPlays.contains(cursor)) {
        ++summary.streakDays;
        --cursor;
    }

    int best = 0;
    int run = 0;
    QList<qint64> sortedDays = daysWithPlays.toList();
    for (int i = 1; i < sortedDays.size(); ++i) {
        const qint64 key = sortedDays.at(i);
        int j = i - 1;
        while (j >= 0 && sortedDays.at(j) > key) {
            sortedDays[j + 1] = sortedDays.at(j);
            --j;
        }
        sortedDays[j + 1] = key;
    }
    for (int i = 0; i < sortedDays.size(); ++i) {
        if (i > 0 && sortedDays.at(i) == sortedDays.at(i - 1) + 1)
            ++run;
        else
            run = 1;
        if (run > best)
            best = run;
    }
    summary.bestStreakDays = qMax(best, summary.streakDays);

    return summary;
}
