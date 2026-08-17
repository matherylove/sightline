#include "ytdlp_setup.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>

#include "sightline_paint.h"
#include "net_transport.h"
#include "sightline_style.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

const char *kReleaseTag = "2026.08.16.082019";
const char *kReleaseBase =
    "https://github.com/nicolaasjan/yt-dlp/releases/download/";

// Qt 5.6 reports Windows versions through QSysInfo::windowsVersion, which
// stops knowing about anything past 8.1; that is fine, because every
// distinction that matters here is below that line.
enum WindowsFamily { FamilyXp, FamilyVista7, FamilyModern };

WindowsFamily detectFamily()
{
#ifdef Q_OS_WIN
    const QSysInfo::WinVersion version = QSysInfo::windowsVersion();
    if (version <= QSysInfo::WV_2003)
        return FamilyXp;
    if (version <= QSysInfo::WV_WINDOWS7)
        return FamilyVista7;
    return FamilyModern;
#else
    return FamilyModern;
#endif
}

bool is64Bit()
{
#ifdef Q_OS_WIN
    // A 32-bit build running on 64-bit Windows is under WOW64, and the
    // 64-bit yt-dlp would still be the better choice there.
    BOOL wow64 = FALSE;
    typedef BOOL (WINAPI *IsWow64Fn)(HANDLE, PBOOL);
    IsWow64Fn isWow64 = reinterpret_cast<IsWow64Fn>(
        GetProcAddress(GetModuleHandleA("kernel32"), "IsWow64Process"));
    if (isWow64 && isWow64(GetCurrentProcess(), &wow64) && wow64)
        return true;
    return sizeof(void *) == 8;
#else
    return sizeof(void *) == 8;
#endif
}

} // namespace

QString YtDlpVariant::releaseTag()
{
    return QString::fromLatin1(kReleaseTag);
}

QString YtDlpVariant::fileName()
{
    switch (detectFamily()) {
    case FamilyXp:
        return QString::fromLatin1("yt-dlp_x86_winXP.exe");
    case FamilyVista7:
        return is64Bit() ? QString::fromLatin1("yt-dlp_win7.exe")
                         : QString::fromLatin1("yt-dlp_x86_win7.exe");
    default:
        break;
    }
    return is64Bit() ? QString::fromLatin1("yt-dlp.exe")
                     : QString::fromLatin1("yt-dlp_x86.exe");
}

QString YtDlpVariant::downloadUrl()
{
    return QString::fromLatin1(kReleaseBase) + releaseTag()
         + QString::fromLatin1("/") + fileName();
}

QString YtDlpVariant::systemLabel()
{
    switch (detectFamily()) {
    case FamilyXp:
        return QString::fromUtf8("Windows XP 32 bits");
    case FamilyVista7:
        return is64Bit() ? QString::fromUtf8("Windows 7 64 bits")
                         : QString::fromUtf8("Windows 7 32 bits");
    default:
        break;
    }
    return is64Bit() ? QString::fromUtf8("Windows 8 en adelante 64 bits")
                     : QString::fromUtf8("Windows 8 en adelante 32 bits");
}

bool YtDlpVariant::isXpBuild()
{
    return detectFamily() == FamilyXp;
}

// --------------------------------------------------------- YtDlpSetupDialog

YtDlpSetupDialog::YtDlpSetupDialog(const QString &targetDirectory, QWidget *parent)
    : SightlineDialog(QString::fromUtf8("Falta yt-dlp"), parent),
      targetDirectory_(targetDirectory),
      network_(0), reply_(0), file_(0),
      statusLabel_(0), progress_(0), downloadButton_(0), closeButton_(0)
{
    setDialogWidth(620);
    network_ = new QNetworkAccessManager(this);

    QVBoxLayout *layout = contentLayout();

    QLabel *heading = new QLabel(
        QString::fromUtf8("Sightline necesita yt-dlp.exe para funcionar"), this);
    heading->setObjectName(QString::fromLatin1("headingLabel"));
    layout->addWidget(heading);

    QLabel *blurb = new QLabel(QString::fromUtf8(
        "Sightline no habla con YouTube directamente: toda la extracción la hace yt-dlp, "
        "que se actualiza reemplazando un archivo sin recompilar nada. Colócalo junto a "
        "Sightline.exe, o deja que lo descargue por ti."), this);
    blurb->setObjectName(QString::fromLatin1("dimLabel"));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);
    layout->addSpacing(8);

    QWidget *box = new QWidget(this);
    box->setStyleSheet(QString::fromLatin1(
        "background: #14191B; border: 1px solid #333E42;"));
    QVBoxLayout *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(14, 12, 14, 12);
    boxLayout->setSpacing(5);

    QLabel *detectedCaption = new QLabel(QString::fromUtf8("Sistema detectado"), box);
    detectedCaption->setFont(SightlinePaint::capsFont(9));
    detectedCaption->setStyleSheet(QString::fromLatin1("color: #4E5D61; border: 0;"));
    boxLayout->addWidget(detectedCaption);

    QLabel *detected = new QLabel(YtDlpVariant::systemLabel(), box);
    detected->setFont(SightlinePaint::uiFont(13, true));
    detected->setStyleSheet(QString::fromLatin1("color: #2FBFAE; border: 0;"));
    boxLayout->addWidget(detected);

    QLabel *asset = new QLabel(
        YtDlpVariant::fileName() + QString::fromUtf8("  ·  versión ")
            + YtDlpVariant::releaseTag(), box);
    asset->setFont(SightlinePaint::monoFont(10));
    asset->setStyleSheet(QString::fromLatin1("color: #7B8A8E; border: 0;"));
    boxLayout->addWidget(asset);

    QLabel *destination = new QLabel(
        QDir::toNativeSeparators(targetDirectory_), box);
    destination->setFont(SightlinePaint::monoFont(10));
    destination->setStyleSheet(QString::fromLatin1("color: #4E5D61; border: 0;"));
    destination->setWordWrap(true);
    boxLayout->addWidget(destination);

    layout->addWidget(box);

    if (!YtDlpVariant::isXpBuild()) {
        // Worth saying out loud: this project exists for XP, and someone
        // testing it elsewhere should know the binary differs.
        QLabel *note = new QLabel(QString::fromUtf8(
            "No estás en Windows XP, así que se descargará el binario correspondiente "
            "a este sistema. El proyecto está pensado para XP, pero funciona igual aquí."), this);
        note->setFont(SightlinePaint::monoFont(10));
        note->setObjectName(QString::fromLatin1("faintLabel"));
        note->setWordWrap(true);
        layout->addWidget(note);
    }

    progress_ = new QProgressBar(this);
    progress_->setFixedHeight(6);
    progress_->setTextVisible(false);
    progress_->setRange(0, 100);
    progress_->setStyleSheet(QString::fromLatin1(
        "QProgressBar { background: #14191B; border: 1px solid #333E42; }"
        "QProgressBar::chunk { background: #2FBFAE; }"));
    progress_->hide();
    layout->addWidget(progress_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setFont(SightlinePaint::monoFont(10));
    statusLabel_->setObjectName(QString::fromLatin1("dimLabel"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    layout->addStretch(1);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(6);

    QPushButton *openFolder = new QPushButton(
        QString::fromUtf8("Abrir la carpeta"), this);
    openFolder->setFixedHeight(22);
    connect(openFolder, SIGNAL(clicked()), this, SLOT(onOpenFolder()));
    buttons->addWidget(openFolder);
    buttons->addStretch(1);

    closeButton_ = new QPushButton(QString::fromUtf8("Ahora no"), this);
    closeButton_->setFixedHeight(22);
    connect(closeButton_, SIGNAL(clicked()), this, SLOT(reject()));
    buttons->addWidget(closeButton_);

    downloadButton_ = new QPushButton(QString::fromUtf8("Descargar ahora"), this);
    downloadButton_->setObjectName(QString::fromLatin1("primaryButton"));
    downloadButton_->setFixedHeight(22);
    connect(downloadButton_, SIGNAL(clicked()), this, SLOT(onDownload()));
    buttons->addWidget(downloadButton_);

    layout->addLayout(buttons);

    if (NetTransport::kind() == NetTransport::None) {
        downloadButton_->setEnabled(false);
        statusLabel_->setText(QString::fromUtf8(
            "Ni Qt ni FFmpeg pueden hacer TLS en este equipo, así que la descarga "
            "automática no es posible. Baja el archivo a mano y déjalo junto a "
            "Sightline.exe con el nombre yt-dlp.exe."));
    } else {
        statusLabel_->setText(QString::fromUtf8(
            "Se descargará desde github.com/nicolaasjan/yt-dlp (unos 17 MB) usando %1.")
            .arg(NetTransport::kind() == NetTransport::QtSsl
                     ? QString::fromLatin1("Qt")
                     : QString::fromLatin1("FFmpeg")));
    }
}

void YtDlpSetupDialog::setBusy(bool busy)
{
    downloadButton_->setEnabled(!busy);
    closeButton_->setText(busy ? QString::fromUtf8("Cancelar")
                               : QString::fromUtf8("Ahora no"));
    progress_->setVisible(busy);
}

void YtDlpSetupDialog::onDownload()
{
    QDir directory(targetDirectory_);
    if (!directory.exists() && !directory.mkpath(QString::fromLatin1("."))) {
        statusLabel_->setText(QString::fromUtf8("No se pudo crear la carpeta de destino."));
        return;
    }

    // Downloaded under a temporary name and renamed only once the transfer
    // completes, so an interrupted download never leaves behind a truncated
    // binary that would fail with a confusing error later.
    const QString finalPath = directory.filePath(QString::fromLatin1("yt-dlp.exe"));
    const QString tempPath = finalPath + QString::fromLatin1(".part");

    file_ = new QFile(tempPath, this);
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        statusLabel_->setText(QString::fromUtf8("No se pudo escribir en ")
                              + QDir::toNativeSeparators(tempPath));
        delete file_;
        file_ = 0;
        return;
    }

    setBusy(true);
    statusLabel_->setText(QString::fromUtf8("Conectando con GitHub…"));

    if (!NetTransport::qtCanDoTls()) {
        // Qt has no TLS, so the transfer goes through FFmpeg's protocol
        // layer. It blocks, but a 17 MB one-off at first run is a fair
        // trade against not being able to install the extractor at all.
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString fetchError;
        const QByteArray payload = NetTransport::fetch(
            YtDlpVariant::downloadUrl(), &fetchError, 120000);
        QApplication::restoreOverrideCursor();

        setBusy(false);
        if (payload.isEmpty()) {
            file_->close();
            file_->remove();
            delete file_;
            file_ = 0;
            statusLabel_->setText(QString::fromUtf8("No se pudo descargar: ") + fetchError);
            return;
        }

        file_->write(payload);
        file_->close();
        const QString tempName = file_->fileName();
        delete file_;
        file_ = 0;

        const QString target = tempName.left(tempName.size() - 5);
        QFile::remove(target);
        if (!QFile::rename(tempName, target)) {
            statusLabel_->setText(QString::fromUtf8(
                "Se descargó pero no se pudo renombrar a yt-dlp.exe."));
            return;
        }
        installedPath_ = QDir::toNativeSeparators(target);
        accept();
        return;
    }

    QNetworkRequest request((QUrl(YtDlpVariant::downloadUrl())));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setRawHeader("User-Agent", "Sightline/0.1");

    reply_ = network_->get(request);
    connect(reply_, SIGNAL(downloadProgress(qint64, qint64)),
            this, SLOT(onProgress(qint64, qint64)));
    connect(reply_, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
    connect(reply_, SIGNAL(finished()), this, SLOT(onFinished()));
}

void YtDlpSetupDialog::onReadyRead()
{
    if (reply_ && file_)
        file_->write(reply_->readAll());
}

void YtDlpSetupDialog::onProgress(qint64 received, qint64 total)
{
    if (total > 0) {
        progress_->setRange(0, 100);
        progress_->setValue(int(received * 100 / total));
        statusLabel_->setText(QString::fromUtf8("Descargando… %1 de %2 MB")
            .arg(received / (1024 * 1024))
            .arg(total / (1024 * 1024)));
    } else {
        progress_->setRange(0, 0);
        statusLabel_->setText(QString::fromUtf8("Descargando… %1 MB")
            .arg(received / (1024 * 1024)));
    }
}

void YtDlpSetupDialog::onFinished()
{
    if (!reply_)
        return;

    const bool ok = (reply_->error() == QNetworkReply::NoError);
    const QString errorText = reply_->errorString();

    if (file_) {
        file_->write(reply_->readAll());
        file_->close();
    }

    reply_->deleteLater();
    reply_ = 0;
    setBusy(false);

    if (!ok) {
        if (file_) {
            file_->remove();
            delete file_;
            file_ = 0;
        }
        statusLabel_->setText(QString::fromUtf8("No se pudo descargar: ") + errorText);
        return;
    }

    const QString tempPath = file_->fileName();
    const QString finalPath = tempPath.left(tempPath.size() - 5);
    QFile::remove(finalPath);

    const bool renamed = QFile::rename(tempPath, finalPath);
    delete file_;
    file_ = 0;

    if (!renamed) {
        statusLabel_->setText(QString::fromUtf8(
            "Se descargó pero no se pudo renombrar. Cierra Sightline y renombra "
            "yt-dlp.exe.part a yt-dlp.exe a mano."));
        return;
    }

    installedPath_ = QDir::toNativeSeparators(finalPath);
    statusLabel_->setText(QString::fromUtf8("Listo. yt-dlp quedó en ") + installedPath_);
    accept();
}

void YtDlpSetupDialog::onOpenFolder()
{
    QDir directory(targetDirectory_);
    if (!directory.exists())
        directory.mkpath(QString::fromLatin1("."));
    QDesktopServices::openUrl(QUrl::fromLocalFile(targetDirectory_));
}
