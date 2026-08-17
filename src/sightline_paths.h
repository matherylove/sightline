#ifndef SIGHTLINE_PATHS_H
#define SIGHTLINE_PATHS_H

#include <QString>

// Every file Sightline writes lives under one root so the whole install can
// be copied to another machine, or deleted, in one move. On XP that lands in
// Application Data; the portable flag puts it next to the executable instead,
// which is what people running this off a USB stick expect.
class SightlinePaths
{
public:
    SightlinePaths();

    bool initialize(QString *error = 0) const;

    QString root() const;
    QString config() const;
    QString library() const;        // subscriptions, playlists, history
    QString cache() const;          // extraction results, thumbnails
    QString thumbnails() const;
    QString segments() const;       // SponsorBlock, kept for offline use
    QString lyrics() const;
    QString downloads() const;      // default target, user can change it
    QString logs() const;
    QString tools() const;          // yt-dlp.exe and qjs.exe live here

    QString settingsFile() const;
    QString libraryFile() const;
    QString historyFile() const;
    QString playLogFile() const;
    QString credentialsFile() const;

    // Full path to a bundled tool, whether it sits in tools/ or beside the
    // executable. Empty when the file is not there at all.
    QString toolPath(const QString &executableName) const;

    // Where the user is told to put yt-dlp.exe: beside Sightline.exe.
    static QString binaryDirectory();

    static bool portableMode();

private:
    QString root_;
};

#endif
