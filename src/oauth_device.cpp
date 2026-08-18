#include "oauth_device.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

const char *kDeviceEndpoint = "https://oauth2.googleapis.com/device/code";
const char *kTokenEndpoint = "https://oauth2.googleapis.com/token";
const char *kApiBase = "https://www.googleapis.com/youtube/v3/";

// Read-only. The app copies data in and never writes back, so asking for
// anything wider would be asking for permission it has no use for.
const char *kScope = "https://www.googleapis.com/auth/youtube.readonly";

QJsonObject readJson(QNetworkReply *reply)
{
    return QJsonDocument::fromJson(reply->readAll()).object();
}

} // namespace

OAuthDevice::OAuthDevice(QObject *parent)
    : QObject(parent), network_(0), pollTimer_(0), expiryTimer_(0),
      interval_(5), expiresIn_(0), importingPlaylists_(false)
{
    network_ = new QNetworkAccessManager(this);

    pollTimer_ = new QTimer(this);
    connect(pollTimer_, SIGNAL(timeout()), this, SLOT(onPollTick()));

    expiryTimer_ = new QTimer(this);
    expiryTimer_->setInterval(1000);
    connect(expiryTimer_, SIGNAL(timeout()), this, SLOT(onPollTick()));
}

void OAuthDevice::setClient(const QString &clientId, const QString &clientSecret)
{
    clientId_ = clientId.trimmed();
    clientSecret_ = clientSecret.trimmed();
}

void OAuthDevice::cancel()
{
    pollTimer_->stop();
    expiryTimer_->stop();
    deviceCode_.clear();
}

void OAuthDevice::begin()
{
    if (clientId_.isEmpty()) {
        emit failed(QString::fromUtf8(
            "Falta el ID de cliente OAuth. Ponlo en config/account.json."));
        return;
    }

    QUrlQuery form;
    form.addQueryItem(QString::fromLatin1("client_id"), clientId_);
    form.addQueryItem(QString::fromLatin1("scope"), QString::fromLatin1(kScope));

    QNetworkRequest request((QUrl(QString::fromLatin1(kDeviceEndpoint))));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QString::fromLatin1("application/x-www-form-urlencoded"));

    QNetworkReply *reply = network_->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, SIGNAL(finished()), this, SLOT(onDeviceCodeReply()));
    emit statusChanged(QString::fromUtf8("Pidiendo código a Google…"));
}

void OAuthDevice::onDeviceCodeReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    const QJsonObject root = readJson(reply);
    if (reply->error() != QNetworkReply::NoError || root.contains(QString::fromLatin1("error"))) {
        const QString detail = root.value(QString::fromLatin1("error_description")).toString();
        emit failed(detail.isEmpty() ? reply->errorString() : detail);
        return;
    }

    deviceCode_ = root.value(QString::fromLatin1("device_code")).toString();
    userCode_ = root.value(QString::fromLatin1("user_code")).toString();
    verificationUrl_ = root.value(QString::fromLatin1("verification_url")).toString();
    if (verificationUrl_.isEmpty())
        verificationUrl_ = root.value(QString::fromLatin1("verification_uri")).toString();
    interval_ = root.value(QString::fromLatin1("interval")).toInt(5);
    expiresIn_ = root.value(QString::fromLatin1("expires_in")).toInt(900);

    if (deviceCode_.isEmpty() || userCode_.isEmpty()) {
        emit failed(QString::fromUtf8("Google no devolvió un código válido."));
        return;
    }

    emit codeReady(userCode_, verificationUrl_, expiresIn_);

    // Google's interval is a floor, not a suggestion: polling faster earns a
    // slow_down error and then a longer wait than if we had obeyed it.
    pollTimer_->start(qMax(5, interval_) * 1000);
    expiryTimer_->start();
}

void OAuthDevice::onPollTick()
{
    QTimer *source = qobject_cast<QTimer *>(sender());
    if (source == expiryTimer_) {
        if (--expiresIn_ <= 0) {
            cancel();
            emit failed(QString::fromUtf8("El código caducó. Vuelve a empezar."));
        }
        return;
    }
    poll();
}

void OAuthDevice::poll()
{
    if (deviceCode_.isEmpty())
        return;

    QUrlQuery form;
    form.addQueryItem(QString::fromLatin1("client_id"), clientId_);
    if (!clientSecret_.isEmpty())
        form.addQueryItem(QString::fromLatin1("client_secret"), clientSecret_);
    form.addQueryItem(QString::fromLatin1("device_code"), deviceCode_);
    form.addQueryItem(QString::fromLatin1("grant_type"),
                      QString::fromLatin1("urn:ietf:params:oauth:grant-type:device_code"));

    QNetworkRequest request((QUrl(QString::fromLatin1(kTokenEndpoint))));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QString::fromLatin1("application/x-www-form-urlencoded"));

    QNetworkReply *reply = network_->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, SIGNAL(finished()), this, SLOT(onTokenReply()));
}

void OAuthDevice::onTokenReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    const QJsonObject root = readJson(reply);
    const QString error = root.value(QString::fromLatin1("error")).toString();

    // authorization_pending is the normal answer while the user is still
    // typing the code on their phone, so it is not reported as a failure.
    if (error == QLatin1String("authorization_pending"))
        return;

    if (error == QLatin1String("slow_down")) {
        interval_ += 5;
        pollTimer_->setInterval(interval_ * 1000);
        return;
    }

    if (error == QLatin1String("access_denied")) {
        cancel();
        emit failed(QString::fromUtf8("Se denegó el acceso desde el otro dispositivo."));
        return;
    }

    if (error == QLatin1String("expired_token")) {
        cancel();
        emit failed(QString::fromUtf8("El código caducó. Vuelve a empezar."));
        return;
    }

    if (!error.isEmpty()) {
        cancel();
        const QString detail = root.value(QString::fromLatin1("error_description")).toString();
        emit failed(detail.isEmpty() ? error : detail);
        return;
    }

    accessToken_ = root.value(QString::fromLatin1("access_token")).toString();
    refreshToken_ = root.value(QString::fromLatin1("refresh_token")).toString();
    const int expires = root.value(QString::fromLatin1("expires_in")).toInt(3600);

    if (accessToken_.isEmpty())
        return;

    cancel();
    emit authorised(accessToken_, refreshToken_, expires);
    importLibrary();
}

void OAuthDevice::importLibrary()
{
    if (accessToken_.isEmpty()) {
        emit failed(QString::fromUtf8("Sin token de acceso."));
        return;
    }
    channels_.clear();
    playlists_.clear();
    importingPlaylists_ = false;
    emit statusChanged(QString::fromUtf8("Copiando suscripciones…"));
    fetchSubscriptions(QString());
}

void OAuthDevice::fetchSubscriptions(const QString &pageToken)
{
    QUrl url(QString::fromLatin1(kApiBase) + QString::fromLatin1("subscriptions"));
    QUrlQuery query;
    query.addQueryItem(QString::fromLatin1("part"), QString::fromLatin1("snippet"));
    query.addQueryItem(QString::fromLatin1("mine"), QString::fromLatin1("true"));
    query.addQueryItem(QString::fromLatin1("maxResults"), QString::fromLatin1("50"));
    if (!pageToken.isEmpty())
        query.addQueryItem(QString::fromLatin1("pageToken"), pageToken);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken_.toUtf8());
    QNetworkReply *reply = network_->get(request);
    reply->setProperty("kind", QString::fromLatin1("subscriptions"));
    connect(reply, SIGNAL(finished()), this, SLOT(onApiReply()));
}

void OAuthDevice::fetchPlaylists(const QString &pageToken)
{
    QUrl url(QString::fromLatin1(kApiBase) + QString::fromLatin1("playlists"));
    QUrlQuery query;
    query.addQueryItem(QString::fromLatin1("part"), QString::fromLatin1("snippet,contentDetails"));
    query.addQueryItem(QString::fromLatin1("mine"), QString::fromLatin1("true"));
    query.addQueryItem(QString::fromLatin1("maxResults"), QString::fromLatin1("50"));
    if (!pageToken.isEmpty())
        query.addQueryItem(QString::fromLatin1("pageToken"), pageToken);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken_.toUtf8());
    QNetworkReply *reply = network_->get(request);
    reply->setProperty("kind", QString::fromLatin1("playlists"));
    connect(reply, SIGNAL(finished()), this, SLOT(onApiReply()));
}

void OAuthDevice::onApiReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    const QString kind = reply->property("kind").toString();
    const QJsonObject root = readJson(reply);

    if (reply->error() != QNetworkReply::NoError) {
        const QJsonObject apiError = root.value(QString::fromLatin1("error")).toObject();
        const QString message = apiError.value(QString::fromLatin1("message")).toString();
        emit failed(message.isEmpty() ? reply->errorString() : message);
        return;
    }

    const QJsonArray items = root.value(QString::fromLatin1("items")).toArray();
    const QString nextPage = root.value(QString::fromLatin1("nextPageToken")).toString();

    if (kind == QLatin1String("subscriptions")) {
        for (int i = 0; i < items.size(); ++i) {
            const QJsonObject snippet = items.at(i).toObject()
                .value(QString::fromLatin1("snippet")).toObject();
            ChannelItem channel;
            channel.id = snippet.value(QString::fromLatin1("resourceId")).toObject()
                .value(QString::fromLatin1("channelId")).toString();
            channel.name = snippet.value(QString::fromLatin1("title")).toString();
            channel.subscribedAt = QDateTime::currentDateTimeUtc();

            const QJsonObject thumbs = snippet.value(QString::fromLatin1("thumbnails")).toObject();
            const QJsonObject medium = thumbs.value(QString::fromLatin1("medium")).toObject();
            channel.avatarUrl = medium.value(QString::fromLatin1("url")).toString();

            if (!channel.id.isEmpty())
                channels_.append(channel);
        }

        if (!nextPage.isEmpty()) {
            emit statusChanged(QString::fromUtf8("Copiando suscripciones… (%1)")
                                   .arg(channels_.size()));
            fetchSubscriptions(nextPage);
            return;
        }

        emit channelsImported(channels_);
        emit statusChanged(QString::fromUtf8("Copiando listas…"));
        importingPlaylists_ = true;
        fetchPlaylists(QString());
        return;
    }

    for (int i = 0; i < items.size(); ++i) {
        const QJsonObject entry = items.at(i).toObject();
        const QJsonObject snippet = entry.value(QString::fromLatin1("snippet")).toObject();
        PlaylistItem playlist;
        playlist.id = entry.value(QString::fromLatin1("id")).toString();
        playlist.title = snippet.value(QString::fromLatin1("title")).toString();
        playlist.ownerName = snippet.value(QString::fromLatin1("channelTitle")).toString();
        playlist.updatedAt = QDateTime::currentDateTimeUtc();
        if (!playlist.id.isEmpty())
            playlists_.append(playlist);
    }

    if (!nextPage.isEmpty()) {
        fetchPlaylists(nextPage);
        return;
    }

    emit playlistsImported(playlists_);

    // Watch history and Watch Later were removed from the Data API in 2016;
    // saying so here beats letting the user hunt for them afterwards.
    emit importFinished(QString::fromUtf8(
        "Se copiaron %1 suscripciones y %2 listas.\n\n"
        "El historial y «Ver más tarde» no están disponibles en la API de YouTube "
        "desde 2016, así que esos siguen siendo locales.")
        .arg(channels_.size()).arg(playlists_.size()));
}
