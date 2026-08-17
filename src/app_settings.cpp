#include "app_settings.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

bool writeJson(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QString::fromUtf8("No se pudo escribir ") + path;
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QString::fromUtf8("No se pudo guardar ") + path;
        return false;
    }
    return true;
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject();
}

} // namespace

AppSettings::AppSettings()
    : formatVersion(1),
      maxHeight(1080),
      avcOnly(true),
      preferAacAudio(true),
      volume(80),
      autoplayNext(true),
      rememberPosition(true),
      sponsorBlockEnabled(true),
      sponsorBlockServer(QString::fromLatin1("https://sponsor.ajay.app")),
      submitSegments(true),
      countTimeSaved(true),
      prefetchSegments(true),
      secondsSaved(0),
      maxConcurrentJobs(2),
      downloadContainer(QString::fromLatin1("mp4")),
      embedThumbnail(true),
      embedMetadata(true),
      embedChapters(true),
      writeSubtitles(false),
      showLyrics(true),
      trackListening(true),
      windowWidth(1080),
      windowHeight(660),
      windowMaximised(false)
{
    // Only the sponsor and the intro go past without asking. Everything else
    // is shown and left to the user, which is the default the mockup states.
    for (int i = 0; i < 8; ++i)
        segmentAction[i] = SegmentPrompt;
    segmentAction[SponsorSegment::Sponsor] = SegmentSkipSilently;
    segmentAction[SponsorSegment::Intro] = SegmentSkipSilently;
    segmentAction[SponsorSegment::MusicOffTopic] = SegmentMarkOnly;
    segmentAction[SponsorSegment::Filler] = SegmentIgnore;
    segmentAction[SponsorSegment::UnknownCategory] = SegmentIgnore;
}

QString AppSettings::formatSelector() const
{
    // Read outwards: best H.264 video within the height cap plus best AAC
    // audio; then the same without the codec constraint; then whatever 
    // single muxed file exists. itag 18 is the last of these and is what
    // still comes back when no PO token provider is configured.
    const QString heightClause = maxHeight > 0
        ? QString::fromLatin1("[height<=?%1]").arg(maxHeight)
        : QString();

    QString selector;
    if (avcOnly) {
        selector = QString::fromLatin1("bv*[vcodec^=avc1]%1+ba[acodec^=mp4a]/")
                       .arg(heightClause);
        selector += QString::fromLatin1("bv*[vcodec^=avc1]%1+ba/").arg(heightClause);
        selector += QString::fromLatin1("b[vcodec^=avc1]%1/").arg(heightClause);
    } else {
        selector = QString::fromLatin1("bv*%1+ba/").arg(heightClause);
    }
    selector += QString::fromLatin1("b%1/b").arg(heightClause);
    return selector;
}

SegmentAction AppSettings::actionFor(SponsorSegment::Category category) const
{
    const int index = int(category);
    if (index < 0 || index >= 8)
        return SegmentIgnore;
    return SegmentAction(segmentAction[index]);
}

void AppSettings::setActionFor(SponsorSegment::Category category, SegmentAction action)
{
    const int index = int(category);
    if (index >= 0 && index < 8)
        segmentAction[index] = int(action);
}

AccountState::AccountState()
    : linked(false), accessTokenExpiry(0)
{
}

bool AccountState::accessTokenValid() const
{
    if (accessToken.isEmpty() || accessTokenExpiry == 0)
        return false;
    // A minute of margin: a token that expires while the request is in
    // flight is indistinguishable from a token that was never valid.
    return QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000 + 60 < accessTokenExpiry;
}

SettingsStore::SettingsStore(const SightlinePaths &paths)
    : paths_(paths)
{
}

AppSettings SettingsStore::load() const
{
    AppSettings settings;
    const QJsonObject root = readJson(paths_.settingsFile());
    if (root.isEmpty()) {
        settings.downloadDirectory = paths_.downloads();
        return settings;
    }

    settings.formatVersion = root.value(QString::fromLatin1("formatVersion")).toInt(1);
    settings.maxHeight = root.value(QString::fromLatin1("maxHeight")).toInt(settings.maxHeight);
    settings.avcOnly = root.value(QString::fromLatin1("avcOnly")).toBool(settings.avcOnly);
    settings.preferAacAudio = root.value(QString::fromLatin1("preferAacAudio")).toBool(settings.preferAacAudio);
    settings.volume = root.value(QString::fromLatin1("volume")).toInt(settings.volume);
    settings.autoplayNext = root.value(QString::fromLatin1("autoplayNext")).toBool(settings.autoplayNext);
    settings.rememberPosition = root.value(QString::fromLatin1("rememberPosition")).toBool(settings.rememberPosition);

    settings.sponsorBlockEnabled = root.value(QString::fromLatin1("sponsorBlockEnabled")).toBool(settings.sponsorBlockEnabled);
    settings.sponsorBlockServer = root.value(QString::fromLatin1("sponsorBlockServer")).toString(settings.sponsorBlockServer);
    settings.submitSegments = root.value(QString::fromLatin1("submitSegments")).toBool(settings.submitSegments);
    settings.countTimeSaved = root.value(QString::fromLatin1("countTimeSaved")).toBool(settings.countTimeSaved);
    settings.prefetchSegments = root.value(QString::fromLatin1("prefetchSegments")).toBool(settings.prefetchSegments);
    settings.secondsSaved = qint64(root.value(QString::fromLatin1("secondsSaved")).toDouble(0));

    const QJsonObject actions = root.value(QString::fromLatin1("segmentActions")).toObject();
    for (int i = 0; i < 8; ++i) {
        const QString key = SponsorSegment::apiName(SponsorSegment::Category(i));
        if (actions.contains(key))
            settings.segmentAction[i] = actions.value(key).toInt(settings.segmentAction[i]);
    }

    settings.ytdlpPath = root.value(QString::fromLatin1("ytdlpPath")).toString();
    settings.jsRuntimePath = root.value(QString::fromLatin1("jsRuntimePath")).toString();
    settings.potProviderUrl = root.value(QString::fromLatin1("potProviderUrl")).toString();
    settings.maxConcurrentJobs = root.value(QString::fromLatin1("maxConcurrentJobs")).toInt(settings.maxConcurrentJobs);

    settings.downloadDirectory = root.value(QString::fromLatin1("downloadDirectory")).toString();
    if (settings.downloadDirectory.isEmpty())
        settings.downloadDirectory = paths_.downloads();
    settings.downloadContainer = root.value(QString::fromLatin1("downloadContainer")).toString(settings.downloadContainer);
    settings.embedThumbnail = root.value(QString::fromLatin1("embedThumbnail")).toBool(settings.embedThumbnail);
    settings.embedMetadata = root.value(QString::fromLatin1("embedMetadata")).toBool(settings.embedMetadata);
    settings.embedChapters = root.value(QString::fromLatin1("embedChapters")).toBool(settings.embedChapters);
    settings.writeSubtitles = root.value(QString::fromLatin1("writeSubtitles")).toBool(settings.writeSubtitles);

    settings.showLyrics = root.value(QString::fromLatin1("showLyrics")).toBool(settings.showLyrics);
    settings.trackListening = root.value(QString::fromLatin1("trackListening")).toBool(settings.trackListening);

    settings.windowWidth = root.value(QString::fromLatin1("windowWidth")).toInt(settings.windowWidth);
    settings.windowHeight = root.value(QString::fromLatin1("windowHeight")).toInt(settings.windowHeight);
    settings.windowMaximised = root.value(QString::fromLatin1("windowMaximised")).toBool(false);

    return settings;
}

bool SettingsStore::save(const AppSettings &settings, QString *error) const
{
    QJsonObject root;
    root.insert(QString::fromLatin1("formatVersion"), settings.formatVersion);
    root.insert(QString::fromLatin1("maxHeight"), settings.maxHeight);
    root.insert(QString::fromLatin1("avcOnly"), settings.avcOnly);
    root.insert(QString::fromLatin1("preferAacAudio"), settings.preferAacAudio);
    root.insert(QString::fromLatin1("volume"), settings.volume);
    root.insert(QString::fromLatin1("autoplayNext"), settings.autoplayNext);
    root.insert(QString::fromLatin1("rememberPosition"), settings.rememberPosition);

    root.insert(QString::fromLatin1("sponsorBlockEnabled"), settings.sponsorBlockEnabled);
    root.insert(QString::fromLatin1("sponsorBlockServer"), settings.sponsorBlockServer);
    root.insert(QString::fromLatin1("submitSegments"), settings.submitSegments);
    root.insert(QString::fromLatin1("countTimeSaved"), settings.countTimeSaved);
    root.insert(QString::fromLatin1("prefetchSegments"), settings.prefetchSegments);
    root.insert(QString::fromLatin1("secondsSaved"), double(settings.secondsSaved));

    QJsonObject actions;
    for (int i = 0; i < 8; ++i)
        actions.insert(SponsorSegment::apiName(SponsorSegment::Category(i)), settings.segmentAction[i]);
    root.insert(QString::fromLatin1("segmentActions"), actions);

    root.insert(QString::fromLatin1("ytdlpPath"), settings.ytdlpPath);
    root.insert(QString::fromLatin1("jsRuntimePath"), settings.jsRuntimePath);
    root.insert(QString::fromLatin1("potProviderUrl"), settings.potProviderUrl);
    root.insert(QString::fromLatin1("maxConcurrentJobs"), settings.maxConcurrentJobs);

    root.insert(QString::fromLatin1("downloadDirectory"), settings.downloadDirectory);
    root.insert(QString::fromLatin1("downloadContainer"), settings.downloadContainer);
    root.insert(QString::fromLatin1("embedThumbnail"), settings.embedThumbnail);
    root.insert(QString::fromLatin1("embedMetadata"), settings.embedMetadata);
    root.insert(QString::fromLatin1("embedChapters"), settings.embedChapters);
    root.insert(QString::fromLatin1("writeSubtitles"), settings.writeSubtitles);

    root.insert(QString::fromLatin1("showLyrics"), settings.showLyrics);
    root.insert(QString::fromLatin1("trackListening"), settings.trackListening);

    root.insert(QString::fromLatin1("windowWidth"), settings.windowWidth);
    root.insert(QString::fromLatin1("windowHeight"), settings.windowHeight);
    root.insert(QString::fromLatin1("windowMaximised"), settings.windowMaximised);

    return writeJson(paths_.settingsFile(), root, error);
}

AccountState SettingsStore::loadAccount() const
{
    AccountState account;
    const QJsonObject root = readJson(paths_.credentialsFile());
    if (root.isEmpty())
        return account;

    account.linked = root.value(QString::fromLatin1("linked")).toBool(false);
    account.accountName = root.value(QString::fromLatin1("accountName")).toString();
    account.refreshToken = root.value(QString::fromLatin1("refreshToken")).toString();
    account.accessToken = root.value(QString::fromLatin1("accessToken")).toString();
    account.accessTokenExpiry = qint64(root.value(QString::fromLatin1("accessTokenExpiry")).toDouble(0));
    account.lastImportSummary = root.value(QString::fromLatin1("lastImportSummary")).toString();
    return account;
}

bool SettingsStore::saveAccount(const AccountState &account, QString *error) const
{
    QJsonObject root;
    root.insert(QString::fromLatin1("linked"), account.linked);
    root.insert(QString::fromLatin1("accountName"), account.accountName);
    root.insert(QString::fromLatin1("refreshToken"), account.refreshToken);
    root.insert(QString::fromLatin1("accessToken"), account.accessToken);
    root.insert(QString::fromLatin1("accessTokenExpiry"), double(account.accessTokenExpiry));
    root.insert(QString::fromLatin1("lastImportSummary"), account.lastImportSummary);
    return writeJson(paths_.credentialsFile(), root, error);
}

bool SettingsStore::clearAccount(QString *error) const
{
    QFile file(paths_.credentialsFile());
    if (!file.exists())
        return true;
    if (!file.remove()) {
        if (error)
            *error = QString::fromUtf8("No se pudo borrar la cuenta guardada.");
        return false;
    }
    return true;
}
