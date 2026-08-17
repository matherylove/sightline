#ifndef SIGHTLINE_SPONSORBLOCK_H
#define SIGHTLINE_SPONSORBLOCK_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "app_settings.h"
#include "media_types.h"
#include "sightline_paths.h"

class QNetworkAccessManager;
class QNetworkReply;

// Segments are fetched when the video opens, not when playback reaches the
// cut, and every response is written to disk. That means a dropped
// connection degrades to "the segments we already know" rather than
// blocking playback on a third party service.
class SponsorBlock : public QObject
{
    Q_OBJECT

public:
    SponsorBlock(const SightlinePaths &paths, const AppSettings &settings, QObject *parent = 0);

    void applySettings(const AppSettings &settings);

    void fetch(const QString &videoId);
    QList<SponsorSegment> segments(const QString &videoId) const;
    bool hasSegments(const QString &videoId) const;

    int cachedSegmentCount() const;
    bool online() const { return online_; }
    QString statusText() const;

signals:
    void segmentsReady(const QString &videoId, const QList<SponsorSegment> &segments);
    void onlineChanged(bool online);

private slots:
    void onFinished(QNetworkReply *reply);

private:
    QString cacheFile(const QString &videoId) const;
    QList<SponsorSegment> readCache(const QString &videoId) const;
    void writeCache(const QString &videoId, const QList<SponsorSegment> &segments) const;

    SightlinePaths paths_;
    AppSettings settings_;
    QNetworkAccessManager *network_;
    QHash<QString, QList<SponsorSegment> > memory_;
    QStringList inFlight_;
    bool online_;
};

#endif
