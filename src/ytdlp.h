#ifndef SIGHTLINE_YTDLP_H
#define SIGHTLINE_YTDLP_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QStringList>

#include "app_settings.h"
#include "media_types.h"
#include "sightline_paths.h"

// Everything that talks to YouTube goes through yt-dlp.exe. Sightline never
// speaks InnerTube itself: the signature ciphers, the n parameter, the client
// impersonation and the PO token dance all change every few weeks, and this
// way that maintenance belongs to a project that keeps up with it.
//
// The rules that matter here, learned the hard way on XP:
//
//  * --ignore-config, or the user's own yt-dlp.conf silently changes the
//    output format and the JSON parse fails with no clue why.
//  * PYTHONIOENCODING=utf-8 plus fromUtf8 on the raw bytes, or every title
//    with an accent arrives mangled through the 1252 console codepage.
//  * SeparateChannels, because warnings on stderr would otherwise be spliced
//    into the middle of the JSON on stdout.
//  * One process per action, never per item: a channel listing is a single
//    --flat-playlist call, not thirty extractions.
//  * A hard cap on live processes. Each one is 40 to 80 MB and XP hands out
//    2 GB of user address space in total.

class QTimer;
class QJsonObject;

class YtDlpJob : public QObject
{
    Q_OBJECT

public:
    enum Kind {
        Version,
        Extract,        // one video, full detail
        Search,
        ChannelFeed,
        PlaylistItems,
        Comments,
        Related
    };

    YtDlpJob(Kind kind, const QString &target, QObject *parent = 0);

    Kind kind() const { return kind_; }
    QString target() const { return target_; }
    QString token() const { return token_; }
    void setToken(const QString &token) { token_ = token; }
    int limit() const { return limit_; }
    void setLimit(int limit) { limit_ = limit; }

private:
    Kind kind_;
    QString target_;
    QString token_;
    int limit_;
};

class YtDlp : public QObject
{
    Q_OBJECT

public:
    enum ChainState {
        ChainReady = 0,
        ChainMissingBinary = 1,
        ChainFallbackInterpreter = 2,   // no qjs.exe, using the Python one
        ChainNoTokenProvider = 3,
        ChainFailing = 4
    };

    YtDlp(const SightlinePaths &paths, const AppSettings &settings, QObject *parent = 0);
    ~YtDlp();

    void applySettings(const AppSettings &settings);

    // Resolved once at start-up: the binary, its version, and whether a
    // JavaScript runtime is reachable. The status bar reads these directly.
    QString binaryPath() const { return binaryPath_; }
    QString version() const { return version_; }
    QString jsRuntimePath() const { return jsRuntimePath_; }
    bool hasJsRuntime() const { return !jsRuntimePath_.isEmpty(); }
    ChainState chainState() const;
    QString chainSummary() const;

    // Warms the file cache and fills in the version. Cheap, and it saves the
    // first real query from paying the cold start on an XP-era disk.
    void probe();

    QString extract(const QString &videoId);
    QString search(const QString &query, int limit = 20);
    QString channelFeed(const QString &channelId, int limit = 30);
    QString playlistItems(const QString &playlistId, int limit = 100);
    QString comments(const QString &videoId, int limit = 40);
    QString related(const QString &videoId, int limit = 12);

    void cancel(const QString &token);
    void cancelAll();
    int activeJobCount() const { return active_.size(); }
    int queuedJobCount() const { return pending_.size(); }

signals:
    void probed();
    void videoReady(const QString &token, const VideoItem &video);
    void listReady(const QString &token, const QList<VideoItem> &videos);
    void commentsReady(const QString &token, const QList<VideoComment> &comments);
    void failed(const QString &token, const QString &message);
    void busyChanged(int active, int queued);

private slots:
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);

private:
    struct Running
    {
        Running() : job(0), process(0) {}
        YtDlpJob *job;
        QProcess *process;
        QByteArray stdOut;
        QByteArray stdErr;
    };

    QString enqueue(YtDlpJob *job);
    void pump();
    void start(YtDlpJob *job);
    void finish(QProcess *process, int exitCode);
    QStringList baseArguments() const;
    QStringList argumentsFor(const YtDlpJob *job) const;
    QProcessEnvironment environment() const;
    void resolveBinaries();

    static VideoItem videoFromJson(const QJsonObject &object);
    static MediaFormat formatFromJson(const QJsonObject &object);
    static VideoComment commentFromJson(const QJsonObject &object);
    static QList<VideoItem> videosFromEntries(const QJsonObject &object);

    SightlinePaths paths_;
    AppSettings settings_;

    QString binaryPath_;
    QString version_;
    QString jsRuntimePath_;
    bool probing_;
    int consecutiveFailures_;

    QQueue<YtDlpJob *> pending_;
    QHash<QProcess *, Running> active_;
    QStringList cancelled_;
    int counter_;
};

#endif
