#include "sightline_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

QString ensure(const QString &path, QString *error)
{
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(QString::fromLatin1("."))) {
        if (error)
            *error = QString::fromUtf8("No se pudo crear la carpeta: ") + path;
        return QString();
    }
    return path;
}

} // namespace

bool SightlinePaths::portableMode()
{
    // A file called portable.txt beside the executable switches the whole
    // app over. It is checked by presence, not contents, so an empty file
    // made with the Explorer right-click menu is enough.
    const QString marker = QCoreApplication::applicationDirPath()
        + QString::fromLatin1("/portable.txt");
    return QFileInfo(marker).exists();
}

SightlinePaths::SightlinePaths()
{
    if (portableMode()) {
        root_ = QCoreApplication::applicationDirPath() + QString::fromLatin1("/data");
        return;
    }

    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QString::fromLatin1("/.sightline");
    root_ = base;
}

bool SightlinePaths::initialize(QString *error) const
{
    const QString directories[] = {
        root(), config(), library(), cache(), thumbnails(),
        segments(), lyrics(), logs(), tools()
    };
    for (int i = 0; i < int(sizeof(directories) / sizeof(directories[0])); ++i) {
        if (ensure(directories[i], error).isEmpty())
            return false;
    }
    return true;
}

QString SightlinePaths::root() const       { return root_; }
QString SightlinePaths::config() const     { return root_ + QString::fromLatin1("/config"); }
QString SightlinePaths::library() const    { return root_ + QString::fromLatin1("/library"); }
QString SightlinePaths::cache() const      { return root_ + QString::fromLatin1("/cache"); }
QString SightlinePaths::thumbnails() const { return cache() + QString::fromLatin1("/thumbnails"); }
QString SightlinePaths::segments() const   { return cache() + QString::fromLatin1("/segments"); }
QString SightlinePaths::lyrics() const     { return cache() + QString::fromLatin1("/lyrics"); }
QString SightlinePaths::logs() const       { return root_ + QString::fromLatin1("/logs"); }
QString SightlinePaths::tools() const      { return root_ + QString::fromLatin1("/tools"); }

QString SightlinePaths::downloads() const
{
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (!movies.isEmpty())
        return movies + QString::fromLatin1("/Sightline");
    return root_ + QString::fromLatin1("/downloads");
}

QString SightlinePaths::settingsFile() const    { return config() + QString::fromLatin1("/settings.json"); }
QString SightlinePaths::libraryFile() const     { return library() + QString::fromLatin1("/library.json"); }
QString SightlinePaths::historyFile() const     { return library() + QString::fromLatin1("/history.json"); }
QString SightlinePaths::playLogFile() const     { return library() + QString::fromLatin1("/playlog.json"); }
QString SightlinePaths::credentialsFile() const { return config() + QString::fromLatin1("/account.json"); }

QString SightlinePaths::toolPath(const QString &executableName) const
{
    // Beside the executable first. That is where the user is told to drop
    // the file and where the built-in downloader puts it, so a stale copy
    // in a data directory must never win over the one they just placed.
    const QString candidates[] = {
        QCoreApplication::applicationDirPath() + QString::fromLatin1("/") + executableName,
        QCoreApplication::applicationDirPath() + QString::fromLatin1("/tools/") + executableName,
        tools() + QString::fromLatin1("/") + executableName
    };
    for (int i = 0; i < int(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        if (QFileInfo(candidates[i]).isFile())
            return QDir::toNativeSeparators(candidates[i]);
    }
    return QString();
}

QString SightlinePaths::binaryDirectory()
{
    return QCoreApplication::applicationDirPath();
}
