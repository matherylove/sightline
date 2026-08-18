#ifndef SIGHTLINE_APP_SETTINGS_H
#define SIGHTLINE_APP_SETTINGS_H

#include <QString>

#include "media_types.h"
#include "sightline_paths.h"

// What SponsorBlock does when it meets a segment. Three actions and nothing
// else: the mockup deliberately has no sensitivity slider, because "how
// aggressively should it skip" is not a question anyone can answer.
enum SegmentAction {
    SegmentSkipSilently = 0,
    SegmentPrompt = 1,
    SegmentMarkOnly = 2,
    SegmentIgnore = 3
};

struct AppSettings
{
    AppSettings();

    int formatVersion;

    // Playback
    int maxHeight;              // 360, 480, 720, 1080; 0 = no cap
    bool avcOnly;               // skip VP9 and AV1, which XP decodes in software
    bool preferAacAudio;        // AAC over Opus for the muxer
    int volume;                 // 0..100
    bool autoplayNext;
    bool rememberPosition;

    // SponsorBlock
    bool sponsorBlockEnabled;
    QString sponsorBlockServer;
    int segmentAction[8];       // indexed by SponsorSegment::Category
    bool submitSegments;
    bool countTimeSaved;
    bool prefetchSegments;
    qint64 secondsSaved;

    // Extraction chain
    QString ytdlpPath;          // empty = look in tools/ and beside the exe
    QString jsRuntimePath;      // qjs.exe; empty = let yt-dlp decide
    QString potProviderUrl;     // http://host:4416 on another machine
    int maxConcurrentJobs;

    // Library and downloads
    QString downloadDirectory;
    QString downloadContainer;  // "mp4", "mkv", "m4a"
    bool embedThumbnail;
    bool embedMetadata;
    bool embedChapters;
    bool writeSubtitles;

    // Music
    bool showLyrics;
    bool trackListening;        // feeds the live stats screen

    // Window
    int windowWidth;
    int windowHeight;
    bool windowMaximised;

    // The yt-dlp format selector this policy amounts to. Kept here so the
    // player, the downloader and the extractor cannot drift apart.
    QString formatSelector() const;

    SegmentAction actionFor(SponsorSegment::Category category) const;
    void setActionFor(SponsorSegment::Category category, SegmentAction action);
};

// Whether the account has been linked, and the refresh token if so. Kept
// apart from the settings file so that wiping the account does not reset
// every preference with it.
struct AccountState
{
    AccountState();

    bool linked;
    QString accountName;
    QString refreshToken;
    QString accessToken;
    qint64 accessTokenExpiry;   // seconds since epoch, UTC
    QString lastImportSummary;

    // Registered by whoever builds this. Google will not issue a client that
    // can ship inside a binary, and a secret in a public repo is revoked
    // within days, so it lives in config/account.json instead.
    QString clientId;
    QString clientSecret;

    bool accessTokenValid() const;
};

class SettingsStore
{
public:
    explicit SettingsStore(const SightlinePaths &paths);

    AppSettings load() const;
    bool save(const AppSettings &settings, QString *error = 0) const;

    AccountState loadAccount() const;
    bool saveAccount(const AccountState &account, QString *error = 0) const;
    bool clearAccount(QString *error = 0) const;

private:
    SightlinePaths paths_;
};

#endif
