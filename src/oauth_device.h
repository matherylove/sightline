#ifndef SIGHTLINE_OAUTH_DEVICE_H
#define SIGHTLINE_OAUTH_DEVICE_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "media_types.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// OAuth 2.0 Device Authorization Grant against Google, then a read-only pass
// over the YouTube Data API to copy subscriptions and playlists in.
//
// This is the only route that satisfies the requirement: the XP machine never
// opens a browser and never sees a password. It does need an OAuth client of
// the "TV and Limited Input" type, which every build has to register for
// itself — Google will not issue one that can be shipped inside a binary, and
// a shared secret in a public repo would be revoked within days. The client
// id lives in config/account.json and the dialog says so when it is absent.
class OAuthDevice : public QObject
{
    Q_OBJECT

public:
    explicit OAuthDevice(QObject *parent = 0);

    void setClient(const QString &clientId, const QString &clientSecret);
    bool hasClient() const { return !clientId_.isEmpty(); }

    void begin();
    void cancel();

    QString userCode() const { return userCode_; }
    QString verificationUrl() const { return verificationUrl_; }
    int secondsRemaining() const { return expiresIn_; }

    // Once authorised: pulls subscriptions, own playlists and the likes list.
    void importLibrary();

    QString accessToken() const { return accessToken_; }
    QString refreshToken() const { return refreshToken_; }

signals:
    void codeReady(const QString &userCode, const QString &verificationUrl, int expiresIn);
    void authorised(const QString &accessToken, const QString &refreshToken, int expiresIn);
    void statusChanged(const QString &message);
    void failed(const QString &message);

    void channelsImported(const QList<ChannelItem> &channels);
    void playlistsImported(const QList<PlaylistItem> &playlists);
    void importFinished(const QString &summary);

private slots:
    void onDeviceCodeReply();
    void onPollTick();
    void onTokenReply();
    void onApiReply();

private:
    void poll();
    void fetchSubscriptions(const QString &pageToken);
    void fetchPlaylists(const QString &pageToken);

    QNetworkAccessManager *network_;
    QTimer *pollTimer_;
    QTimer *expiryTimer_;

    QString clientId_;
    QString clientSecret_;
    QString deviceCode_;
    QString userCode_;
    QString verificationUrl_;
    QString accessToken_;
    QString refreshToken_;
    int interval_;
    int expiresIn_;

    QList<ChannelItem> channels_;
    QList<PlaylistItem> playlists_;
    bool importingPlaylists_;
};

#endif
