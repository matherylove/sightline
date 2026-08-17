#include "ytdlp.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcessEnvironment>

namespace {

// yt-dlp writes one JSON object per line for playlists and a single object
// for a video. Both are handled by splitting on newlines and parsing each
// non-empty line, which also survives a stray progress line slipping through.
QList<QJsonObject> parseJsonLines(const QByteArray &data)
{
    QList<QJsonObject> objects;
    const QList<QByteArray> lines = data.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty() || line.at(0) != '{')
            continue;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            objects.append(document.object());
    }
    return objects;
}

QString stringValue(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isString() ? value.toString() : QString();
}

qint64 intValue(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isDouble())
        return qint64(value.toDouble());
    if (value.isString())
        return value.toString().toLongLong();
    return 0;
}

double doubleValue(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isDouble())
        return value.toDouble();
    if (value.isString())
        return value.toString().toDouble();
    return 0.0;
}

// upload_date arrives as YYYYMMDD. timestamp, when present, is better,
// because it carries the hour and the "hace 2 h" label depends on it.
QDateTime dateFromJson(const QJsonObject &object)
{
    const qint64 timestamp = intValue(object, "timestamp");
    if (timestamp > 0)
        return QDateTime::fromMSecsSinceEpoch(timestamp * 1000LL, Qt::UTC);

    const QString compact = stringValue(object, "upload_date");
    if (compact.size() == 8) {
        const QDate date(compact.left(4).toInt(),
                         compact.mid(4, 2).toInt(),
                         compact.mid(6, 2).toInt());
        if (date.isValid())
            return QDateTime(date, QTime(0, 0), Qt::UTC);
    }
    return QDateTime();
}

QString pickThumbnail(const QJsonObject &object)
{
    // hqdefault.jpg over the WebP variants on purpose: Qt 5.6 does not ship
    // a WebP image plugin, and a static build will not gain one.
    const QString direct = stringValue(object, "thumbnail");
    if (!direct.isEmpty() && !direct.contains(QLatin1String(".webp")))
        return direct;

    const QJsonArray candidates = object.value(QLatin1String("thumbnails")).toArray();
    QString best;
    int bestWidth = 0;
    for (int i = 0; i < candidates.size(); ++i) {
        const QJsonObject entry = candidates.at(i).toObject();
        const QString url = stringValue(entry, "url");
        if (url.isEmpty() || url.contains(QLatin1String(".webp")))
            continue;
        const int width = int(intValue(entry, "width"));
        if (width > bestWidth && width <= 640) {
            bestWidth = width;
            best = url;
        }
    }
    if (!best.isEmpty())
        return best;

    const QString id = stringValue(object, "id");
    if (!id.isEmpty())
        return QString::fromLatin1("https://i.ytimg.com/vi/") + id + QString::fromLatin1("/hqdefault.jpg");
    return QString();
}

} // namespace

// -------------------------------------------------------------------- YtDlpJob

YtDlpJob::YtDlpJob(Kind kind, const QString &target, QObject *parent)
    : QObject(parent), kind_(kind), target_(target), limit_(20)
{
}

// ----------------------------------------------------------------------- YtDlp

YtDlp::YtDlp(const SightlinePaths &paths, const AppSettings &settings, QObject *parent)
    : QObject(parent),
      paths_(paths),
      settings_(settings),
      probing_(false),
      consecutiveFailures_(0),
      counter_(0)
{
    resolveBinaries();
}

YtDlp::~YtDlp()
{
    // Orphaned yt-dlp processes pile up on XP and each one holds tens of
    // megabytes, so the destructor kills anything still running rather than
    // trusting them to notice the parent has gone.
    cancelAll();
}

void YtDlp::applySettings(const AppSettings &settings)
{
    settings_ = settings;
    resolveBinaries();
}

void YtDlp::resolveBinaries()
{
    binaryPath_ = settings_.ytdlpPath;
    if (binaryPath_.isEmpty() || !QFileInfo(binaryPath_).isFile())
        binaryPath_ = paths_.toolPath(QString::fromLatin1("yt-dlp.exe"));
    if (binaryPath_.isEmpty())
        binaryPath_ = paths_.toolPath(QString::fromLatin1("yt-dlp"));

    jsRuntimePath_ = settings_.jsRuntimePath;
    if (jsRuntimePath_.isEmpty() || !QFileInfo(jsRuntimePath_).isFile()) {
        jsRuntimePath_ = paths_.toolPath(QString::fromLatin1("qjs.exe"));
        if (jsRuntimePath_.isEmpty())
            jsRuntimePath_ = paths_.toolPath(QString::fromLatin1("qjs"));
    }
}

YtDlp::ChainState YtDlp::chainState() const
{
    if (binaryPath_.isEmpty())
        return ChainMissingBinary;
    if (consecutiveFailures_ >= 3)
        return ChainFailing;
    if (jsRuntimePath_.isEmpty())
        return ChainFallbackInterpreter;
    if (settings_.potProviderUrl.isEmpty())
        return ChainNoTokenProvider;
    return ChainReady;
}

QString YtDlp::chainSummary() const
{
    switch (chainState()) {
    case ChainMissingBinary:
        return QString::fromUtf8("Falta yt-dlp.exe en la carpeta tools");
    case ChainFailing:
        return QString::fromUtf8("La extracción falla de forma repetida");
    case ChainFallbackInterpreter:
        return QString::fromUtf8("Sin qjs.exe: se usa el intérprete interno, más lento");
    case ChainNoTokenProvider:
        return QString::fromUtf8("Sin proveedor de tokens: calidad limitada");
    default:
        break;
    }
    return QString::fromUtf8("Cadena de extracción completa");
}

void YtDlp::probe()
{
    if (binaryPath_.isEmpty() || probing_)
        return;
    probing_ = true;
    enqueue(new YtDlpJob(YtDlpJob::Version, QString(), this));
}

QString YtDlp::extract(const QString &videoId)
{
    return enqueue(new YtDlpJob(YtDlpJob::Extract, videoId, this));
}

QString YtDlp::search(const QString &query, int limit)
{
    YtDlpJob *job = new YtDlpJob(YtDlpJob::Search, query, this);
    job->setLimit(limit);
    return enqueue(job);
}

QString YtDlp::channelFeed(const QString &channelId, int limit)
{
    YtDlpJob *job = new YtDlpJob(YtDlpJob::ChannelFeed, channelId, this);
    job->setLimit(limit);
    return enqueue(job);
}

QString YtDlp::playlistItems(const QString &playlistId, int limit)
{
    YtDlpJob *job = new YtDlpJob(YtDlpJob::PlaylistItems, playlistId, this);
    job->setLimit(limit);
    return enqueue(job);
}

QString YtDlp::comments(const QString &videoId, int limit)
{
    YtDlpJob *job = new YtDlpJob(YtDlpJob::Comments, videoId, this);
    job->setLimit(limit);
    return enqueue(job);
}

QString YtDlp::related(const QString &videoId, int limit)
{
    YtDlpJob *job = new YtDlpJob(YtDlpJob::Related, videoId, this);
    job->setLimit(limit);
    return enqueue(job);
}

QString YtDlp::enqueue(YtDlpJob *job)
{
    const QString token = QString::fromLatin1("job%1").arg(++counter_);
    job->setToken(token);
    pending_.enqueue(job);
    pump();
    emit busyChanged(active_.size(), pending_.size());
    return token;
}

void YtDlp::cancel(const QString &token)
{
    // A pending job can simply be dropped. A running one has to be killed,
    // and its token remembered so the finish handler stays quiet about it.
    QQueue<YtDlpJob *> keep;
    while (!pending_.isEmpty()) {
        YtDlpJob *job = pending_.dequeue();
        if (job->token() == token)
            job->deleteLater();
        else
            keep.enqueue(job);
    }
    pending_ = keep;

    QHash<QProcess *, Running>::const_iterator it = active_.constBegin();
    for (; it != active_.constEnd(); ++it) {
        if (it.value().job && it.value().job->token() == token) {
            cancelled_.append(token);
            it.key()->kill();
            break;
        }
    }
    emit busyChanged(active_.size(), pending_.size());
}

void YtDlp::cancelAll()
{
    while (!pending_.isEmpty())
        pending_.dequeue()->deleteLater();

    const QList<QProcess *> processes = active_.keys();
    for (int i = 0; i < processes.size(); ++i) {
        const Running &running = active_.value(processes.at(i));
        if (running.job)
            cancelled_.append(running.job->token());
        processes.at(i)->kill();
        processes.at(i)->waitForFinished(1500);
    }
}

void YtDlp::pump()
{
    const int limit = qMax(1, settings_.maxConcurrentJobs);
    while (!pending_.isEmpty() && active_.size() < limit)
        start(pending_.dequeue());
}

QProcessEnvironment YtDlp::environment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // Without this every accented title arrives through the console codepage
    // and comes out as mojibake. It has to be set on the child, not on us.
    env.insert(QString::fromLatin1("PYTHONIOENCODING"), QString::fromLatin1("utf-8"));
    env.insert(QString::fromLatin1("PYTHONUTF8"), QString::fromLatin1("1"));

    // yt-dlp finds its JavaScript runtime on PATH, so qjs.exe is put at the
    // front of the child's PATH rather than passed as a flag.
    if (!jsRuntimePath_.isEmpty()) {
        const QString runtimeDir = QDir::toNativeSeparators(QFileInfo(jsRuntimePath_).absolutePath());
        const QString existing = env.value(QString::fromLatin1("PATH"));
        env.insert(QString::fromLatin1("PATH"), runtimeDir + QString::fromLatin1(";") + existing);
    }
    return env;
}

QStringList YtDlp::baseArguments() const
{
    QStringList arguments;

    // Without --ignore-config the user's own yt-dlp.conf changes the output
    // shape and the JSON parse fails with nothing useful to show them.
    arguments << QString::fromLatin1("--ignore-config")
              << QString::fromLatin1("--no-warnings")
              << QString::fromLatin1("--no-progress")
              << QString::fromLatin1("--no-update")
              << QString::fromLatin1("--no-playlist-reverse")
              << QString::fromLatin1("--socket-timeout") << QString::fromLatin1("20")
              << QString::fromLatin1("--retries") << QString::fromLatin1("2");

    // The cache holds the deciphered player functions. Without it every call
    // re-downloads and re-processes the player JS, which is the slow part.
    arguments << QString::fromLatin1("--cache-dir")
              << QDir::toNativeSeparators(paths_.cache() + QString::fromLatin1("/ytdlp"));

    if (!settings_.potProviderUrl.isEmpty()) {
        arguments << QString::fromLatin1("--extractor-args")
                  << QString::fromLatin1("youtubepot-bgutilhttp:base_url=") + settings_.potProviderUrl;
    }

    return arguments;
}

QStringList YtDlp::argumentsFor(const YtDlpJob *job) const
{
    QStringList arguments = baseArguments();
    const QString limit = QString::number(job->limit());

    switch (job->kind()) {
    case YtDlpJob::Version:
        return QStringList() << QString::fromLatin1("--version");

    case YtDlpJob::Extract:
        arguments << QString::fromLatin1("--dump-single-json")
                  << QString::fromLatin1("--no-playlist")
                  << (QString::fromLatin1("https://www.youtube.com/watch?v=") + job->target());
        break;

    case YtDlpJob::Search:
        arguments << QString::fromLatin1("--dump-json")
                  << QString::fromLatin1("--flat-playlist")
                  << (QString::fromLatin1("ytsearch") + limit + QString::fromLatin1(":") + job->target());
        break;

    case YtDlpJob::ChannelFeed:
        arguments << QString::fromLatin1("--dump-json")
                  << QString::fromLatin1("--flat-playlist")
                  << QString::fromLatin1("--playlist-end") << limit
                  << (job->target().startsWith(QLatin1String("http"))
                          ? job->target()
                          : QString::fromLatin1("https://www.youtube.com/channel/") + job->target()
                                + QString::fromLatin1("/videos"));
        break;

    case YtDlpJob::PlaylistItems:
        arguments << QString::fromLatin1("--dump-json")
                  << QString::fromLatin1("--flat-playlist")
                  << QString::fromLatin1("--playlist-end") << limit
                  << (QString::fromLatin1("https://www.youtube.com/playlist?list=") + job->target());
        break;

    case YtDlpJob::Comments:
        // Comments are a separate, slow round trip. The UI only asks for
        // them when the tab is opened, and says so on screen.
        arguments << QString::fromLatin1("--dump-single-json")
                  << QString::fromLatin1("--write-comments")
                  << QString::fromLatin1("--no-playlist")
                  << QString::fromLatin1("--extractor-args")
                  << (QString::fromLatin1("youtube:comment_sort=top;max_comments=") + limit
                      + QString::fromLatin1(",all,") + limit + QString::fromLatin1(",10"))
                  << (QString::fromLatin1("https://www.youtube.com/watch?v=") + job->target());
        break;

    case YtDlpJob::Related:
        // The watch page carries its own recommendations; asking for the
        // mix playlist is the cheapest way to get them without a second
        // extraction of every entry.
        arguments << QString::fromLatin1("--dump-json")
                  << QString::fromLatin1("--flat-playlist")
                  << QString::fromLatin1("--playlist-end") << limit
                  << (QString::fromLatin1("https://www.youtube.com/watch?v=") + job->target()
                      + QString::fromLatin1("&list=RD") + job->target());
        break;
    }

    return arguments;
}

void YtDlp::start(YtDlpJob *job)
{
    if (binaryPath_.isEmpty()) {
        const QString token = job->token();
        job->deleteLater();
        emit failed(token, QString::fromUtf8(
            "No se encuentra yt-dlp.exe. Colócalo en la carpeta tools junto a Sightline."));
        return;
    }

    QProcess *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProcessEnvironment(environment());
    process->setWorkingDirectory(QFileInfo(binaryPath_).absolutePath());

    Running running;
    running.job = job;
    running.process = process;
    active_.insert(process, running);

    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(onFinished(int, QProcess::ExitStatus)));
    connect(process, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(onError(QProcess::ProcessError)));

    process->start(binaryPath_, argumentsFor(job));
}

void YtDlp::onError(QProcess::ProcessError)
{
    QProcess *process = qobject_cast<QProcess *>(sender());
    if (process)
        finish(process, -1);
}

void YtDlp::onFinished(int exitCode, QProcess::ExitStatus)
{
    QProcess *process = qobject_cast<QProcess *>(sender());
    if (process)
        finish(process, exitCode);
}

void YtDlp::finish(QProcess *process, int exitCode)
{
    if (!active_.contains(process))
        return;

    Running running = active_.take(process);
    running.stdOut = process->readAllStandardOutput();
    running.stdErr = process->readAllStandardError();

    YtDlpJob *job = running.job;
    const QString token = job ? job->token() : QString();
    const YtDlpJob::Kind kind = job ? job->kind() : YtDlpJob::Version;

    process->deleteLater();
    if (job)
        job->deleteLater();

    const bool wasCancelled = cancelled_.removeOne(token);

    if (!wasCancelled) {
        if (exitCode != 0 && running.stdOut.trimmed().isEmpty()) {
            ++consecutiveFailures_;
            QString message = QString::fromUtf8(running.stdErr).trimmed();
            if (message.isEmpty())
                message = QString::fromUtf8("yt-dlp terminó con el código %1.").arg(exitCode);
            // The stderr of a failed extraction can be dozens of lines; the
            // last one is almost always the actual reason.
            const QStringList lines = message.split(QLatin1Char('\n'), QString::SkipEmptyParts);
            if (!lines.isEmpty())
                message = lines.last().trimmed();
            emit failed(token, message);
        } else {
            consecutiveFailures_ = 0;

            if (kind == YtDlpJob::Version) {
                version_ = QString::fromUtf8(running.stdOut).trimmed();
                probing_ = false;
                emit probed();
            } else if (kind == YtDlpJob::Extract) {
                const QList<QJsonObject> objects = parseJsonLines(running.stdOut);
                if (objects.isEmpty())
                    emit failed(token, QString::fromUtf8("La respuesta de yt-dlp no era JSON válido."));
                else
                    emit videoReady(token, videoFromJson(objects.first()));
            } else if (kind == YtDlpJob::Comments) {
                const QList<QJsonObject> objects = parseJsonLines(running.stdOut);
                QList<VideoComment> comments;
                if (!objects.isEmpty()) {
                    const QJsonArray array = objects.first().value(QLatin1String("comments")).toArray();
                    for (int i = 0; i < array.size(); ++i)
                        comments.append(commentFromJson(array.at(i).toObject()));
                }
                emit commentsReady(token, comments);
            } else {
                const QList<QJsonObject> objects = parseJsonLines(running.stdOut);
                QList<VideoItem> videos;
                for (int i = 0; i < objects.size(); ++i) {
                    const QJsonObject object = objects.at(i);
                    if (object.contains(QLatin1String("entries"))) {
                        videos.append(videosFromEntries(object));
                        continue;
                    }
                    const VideoItem video = videoFromJson(object);
                    if (!video.id.isEmpty())
                        videos.append(video);
                }
                emit listReady(token, videos);
            }
        }
    }

    pump();
    emit busyChanged(active_.size(), pending_.size());
}

// ---------------------------------------------------------------- conversion

MediaFormat YtDlp::formatFromJson(const QJsonObject &object)
{
    MediaFormat format;
    format.itag = stringValue(object, "format_id");
    format.url = stringValue(object, "url");
    format.extension = stringValue(object, "ext");
    format.videoCodec = stringValue(object, "vcodec");
    format.audioCodec = stringValue(object, "acodec");
    format.width = int(intValue(object, "width"));
    format.height = int(intValue(object, "height"));
    format.fps = doubleValue(object, "fps");
    format.note = stringValue(object, "format_note");

    // tbr is in kbit/s and is the only rate present on many formats.
    const double tbr = doubleValue(object, "tbr");
    if (tbr > 0.0)
        format.bitrate = qint64(tbr * 1000.0);

    format.fileSize = intValue(object, "filesize");
    if (format.fileSize == 0)
        format.fileSize = intValue(object, "filesize_approx");

    return format;
}

VideoItem YtDlp::videoFromJson(const QJsonObject &object)
{
    VideoItem video;
    video.id = stringValue(object, "id");
    video.title = stringValue(object, "title");
    video.channelId = stringValue(object, "channel_id");
    if (video.channelId.isEmpty())
        video.channelId = stringValue(object, "uploader_id");
    video.channelName = stringValue(object, "channel");
    if (video.channelName.isEmpty())
        video.channelName = stringValue(object, "uploader");
    video.description = stringValue(object, "description");
    video.duration = intValue(object, "duration");
    video.viewCount = intValue(object, "view_count");
    video.likeCount = intValue(object, "like_count");
    video.published = dateFromJson(object);
    video.thumbnailUrl = pickThumbnail(object);

    const QJsonArray formats = object.value(QLatin1String("formats")).toArray();
    for (int i = 0; i < formats.size(); ++i) {
        const MediaFormat format = formatFromJson(formats.at(i).toObject());
        if (!format.url.isEmpty())
            video.formats.append(format);
    }

    const QJsonArray chapters = object.value(QLatin1String("chapters")).toArray();
    for (int i = 0; i < chapters.size(); ++i) {
        const QJsonObject entry = chapters.at(i).toObject();
        VideoChapter chapter;
        chapter.title = stringValue(entry, "title");
        chapter.start = doubleValue(entry, "start_time");
        chapter.end = doubleValue(entry, "end_time");
        video.chapters.append(chapter);
    }

    video.detailed = !video.formats.isEmpty();
    if (video.detailed)
        video.urlsFetchedAt = QDateTime::currentDateTimeUtc();

    return video;
}

QList<VideoItem> YtDlp::videosFromEntries(const QJsonObject &object)
{
    QList<VideoItem> videos;
    const QJsonArray entries = object.value(QLatin1String("entries")).toArray();
    for (int i = 0; i < entries.size(); ++i) {
        const QJsonObject entry = entries.at(i).toObject();
        if (entry.isEmpty())
            continue;
        const VideoItem video = videoFromJson(entry);
        if (!video.id.isEmpty())
            videos.append(video);
    }
    return videos;
}

VideoComment YtDlp::commentFromJson(const QJsonObject &object)
{
    VideoComment comment;
    comment.id = stringValue(object, "id");
    comment.parentId = stringValue(object, "parent");
    comment.author = stringValue(object, "author");
    comment.text = stringValue(object, "text");
    comment.likeCount = int(intValue(object, "like_count"));
    comment.replyCount = int(intValue(object, "reply_count"));
    comment.authorIsUploader = object.value(QLatin1String("author_is_uploader")).toBool(false);
    comment.pinned = object.value(QLatin1String("is_pinned")).toBool(false);

    const qint64 timestamp = intValue(object, "timestamp");
    if (timestamp > 0)
        comment.published = QDateTime::fromMSecsSinceEpoch(timestamp * 1000LL, Qt::UTC);

    return comment;
}
