#ifndef SIGHTLINE_YTDLP_SETUP_H
#define SIGHTLINE_YTDLP_SETUP_H

#include <QString>

#include "sightline_window.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;

// Which yt-dlp build this machine needs.
//
// The XP-specific build exists because upstream dropped XP long ago; the
// nicolaasjan fork keeps one alive and tracks upstream. Picking the wrong
// one is not a soft failure: a Windows 8 binary on XP dies with "not a valid
// Win32 application" and tells the user nothing useful, so the choice is
// made here from the actual OS version rather than left to them.
class YtDlpVariant
{
public:
    static QString releaseTag();
    static QString downloadUrl();
    static QString fileName();
    static QString systemLabel();

    // True when this is the Windows XP build, which is the only one that
    // will start on the machine this project targets.
    static bool isXpBuild();
};

// Shown at startup when yt-dlp.exe is not beside Sightline.exe. Offers to
// fetch the right build rather than making the user work out which of five
// assets applies to them.
class YtDlpSetupDialog : public SightlineDialog
{
    Q_OBJECT

public:
    YtDlpSetupDialog(const QString &targetDirectory, QWidget *parent = 0);

    QString installedPath() const { return installedPath_; }

private slots:
    void onDownload();
    void onProgress(qint64 received, qint64 total);
    void onReadyRead();
    void onFinished();
    void onOpenFolder();

private:
    void setBusy(bool busy);

    QString targetDirectory_;
    QString installedPath_;

    QNetworkAccessManager *network_;
    QNetworkReply *reply_;
    QFile *file_;

    QLabel *statusLabel_;
    QProgressBar *progress_;
    QPushButton *downloadButton_;
    QPushButton *closeButton_;
};

#endif
