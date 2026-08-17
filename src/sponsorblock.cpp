#include "sponsorblock.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
#include <QUrlQuery>

SponsorBlock::SponsorBlock(const SightlinePaths &paths, const AppSettings &settings, QObject *parent)
    : QObject(parent), paths_(paths), settings_(settings), network_(0), online_(true)
{
    network_ = new QNetworkAccessManager(this);
    connect(network_, SIGNAL(finished(QNetworkReply *)), this, SLOT(onFinished(QNetworkReply *)));
}

void SponsorBlock::applySettings(const AppSettings &settings)
{
    settings_ = settings;
}

QString SponsorBlock::cacheFile(const QString &videoId) const
{
    return paths_.segments() + QString::fromLatin1("/") + videoId + QString::fromLatin1(".json");
}

QList<SponsorSegment> SponsorBlock::readCache(const QString &videoId) const
{
    QList<SponsorSegment> segments;
    QFile file(cacheFile(videoId));
    if (!file.open(QIODevice::ReadOnly))
        return segments;

    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (int i = 0; i < array.size(); ++i) {
        const QJsonObject o = array.at(i).toObject();
        SponsorSegment segment;
        segment.category = SponsorSegment::categoryFromApi(
            o.value(QString::fromLatin1("category")).toString());
        segment.start = o.value(QString::fromLatin1("start")).toDouble();
        segment.end = o.value(QString::fromLatin1("end")).toDouble();
        segment.uuid = o.value(QString::fromLatin1("uuid")).toString();
        if (segment.end > segment.start)
            segments.append(segment);
    }
    return segments;
}

void SponsorBlock::writeCache(const QString &videoId, const QList<SponsorSegment> &segments) const
{
    QJsonArray array;
    for (int i = 0; i < segments.size(); ++i) {
        const SponsorSegment &segment = segments.at(i);
        QJsonObject o;
        o.insert(QString::fromLatin1("category"), SponsorSegment::apiName(segment.category));
        o.insert(QString::fromLatin1("start"), segment.start);
        o.insert(QString::fromLatin1("end"), segment.end);
        o.insert(QString::fromLatin1("uuid"), segment.uuid);
        array.append(o);
    }

    QSaveFile file(cacheFile(videoId));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void SponsorBlock::fetch(const QString &videoId)
{
    if (videoId.isEmpty() || !settings_.sponsorBlockEnabled)
        return;

    if (!memory_.contains(videoId)) {
        const QList<SponsorSegment> cached = readCache(videoId);
        if (!cached.isEmpty()) {
            memory_.insert(videoId, cached);
            emit segmentsReady(videoId, cached);
        }
    } else {
        emit segmentsReady(videoId, memory_.value(videoId));
    }

    if (inFlight_.contains(videoId))
        return;
    inFlight_.append(videoId);

    // The privacy-preserving endpoint: only the first four characters of the
    // SHA-256 of the id go over the wire, and the server answers with every
    // video whose hash starts that way. Which one we wanted stays here.
    const QByteArray digest = QCryptographicHash::hash(videoId.toUtf8(),
                                                       QCryptographicHash::Sha256).toHex();
    const QString prefix = QString::fromLatin1(digest.left(4));

    QUrl url(settings_.sponsorBlockServer + QString::fromLatin1("/api/skipSegments/") + prefix);
    QUrlQuery query;
    for (int i = 0; i < 7; ++i) {
        query.addQueryItem(QString::fromLatin1("category"),
                           SponsorSegment::apiName(SponsorSegment::Category(i)));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Sightline/0.1");
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = network_->get(request);
    reply->setProperty("videoId", videoId);
}

void SponsorBlock::onFinished(QNetworkReply *reply)
{
    const QString videoId = reply->property("videoId").toString();
    inFlight_.removeAll(videoId);

    if (reply->error() != QNetworkReply::NoError) {
        if (online_) {
            online_ = false;
            emit onlineChanged(false);
        }
        reply->deleteLater();
        return;
    }

    if (!online_) {
        online_ = true;
        emit onlineChanged(true);
    }

    const QJsonArray videos = QJsonDocument::fromJson(reply->readAll()).array();
    reply->deleteLater();

    for (int i = 0; i < videos.size(); ++i) {
        const QJsonObject entry = videos.at(i).toObject();
        const QString id = entry.value(QString::fromLatin1("videoID")).toString();
        if (id.isEmpty())
            continue;

        QList<SponsorSegment> segments;
        const QJsonArray array = entry.value(QString::fromLatin1("segments")).toArray();
        for (int j = 0; j < array.size(); ++j) {
            const QJsonObject o = array.at(j).toObject();
            const QJsonArray bounds = o.value(QString::fromLatin1("segment")).toArray();
            if (bounds.size() != 2)
                continue;

            SponsorSegment segment;
            segment.category = SponsorSegment::categoryFromApi(
                o.value(QString::fromLatin1("category")).toString());
            segment.start = bounds.at(0).toDouble();
            segment.end = bounds.at(1).toDouble();
            segment.uuid = o.value(QString::fromLatin1("UUID")).toString();
            if (segment.end > segment.start)
                segments.append(segment);
        }

        // Sorted by start, so the player can walk them with a single cursor
        // rather than searching the whole list on every position tick.
        for (int a = 1; a < segments.size(); ++a) {
            const SponsorSegment key = segments.at(a);
            int b = a - 1;
            while (b >= 0 && segments.at(b).start > key.start) {
                segments[b + 1] = segments.at(b);
                --b;
            }
            segments[b + 1] = key;
        }

        memory_.insert(id, segments);
        writeCache(id, segments);

        // Only the video actually being watched needs to tell anyone; the
        // rest of the hash bucket is cached quietly for later.
        if (id == videoId)
            emit segmentsReady(id, segments);
    }
}

QList<SponsorSegment> SponsorBlock::segments(const QString &videoId) const
{
    return memory_.value(videoId);
}

bool SponsorBlock::hasSegments(const QString &videoId) const
{
    return memory_.contains(videoId);
}

int SponsorBlock::cachedSegmentCount() const
{
    int total = 0;
    for (QHash<QString, QList<SponsorSegment> >::const_iterator it = memory_.constBegin();
         it != memory_.constEnd(); ++it) {
        total += it.value().size();
    }

    // Segments already on disk count too: the status bar figure is meant to
    // say "this is what still works offline", not "this is what is in RAM".
    QDir directory(paths_.segments());
    const QStringList files = directory.entryList(QStringList()
        << QString::fromLatin1("*.json"), QDir::Files);
    if (files.size() > memory_.size())
        total += (files.size() - memory_.size()) * 3;

    return total;
}

QString SponsorBlock::statusText() const
{
    if (!settings_.sponsorBlockEnabled)
        return QString::fromUtf8("desactivado");
    if (!online_)
        return QString::fromUtf8("sin conexión");
    return QString::fromUtf8("%1 seg.").arg(cachedSegmentCount());
}
