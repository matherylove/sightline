#include <QApplication>
#include <QFont>
#include <QMessageBox>
#include <QTextCodec>

#include "main_window.h"
#include "os_capabilities.h"
#include "media_types.h"
#include "sightline_style.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QString::fromLatin1("Sightline"));
    application.setApplicationVersion(QString::fromLatin1("0.1.0"));
    application.setOrganizationName(QString::fromLatin1("Sightline"));

    // The whole app assumes UTF-8 internally; on XP the system codec would
    // otherwise be 1252 and every accented title would round-trip wrong.
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    // Tahoma at 11px is the actual Windows XP interface font, and the entire
    // layout was drawn against its metrics.
    QFont interfaceFont(QString::fromLatin1("Tahoma"));
    interfaceFont.setPixelSize(11);
    application.setFont(interfaceFont);

    // Queued connections carry these across threads, and Qt refuses to
    // marshal a type it has not been told about — silently, at runtime.
    qRegisterMetaType<VideoItem>("VideoItem");
    qRegisterMetaType<ChannelItem>("ChannelItem");
    qRegisterMetaType<PlaylistItem>("PlaylistItem");
    qRegisterMetaType<VideoComment>("VideoComment");
    qRegisterMetaType<SponsorSegment>("SponsorSegment");
    qRegisterMetaType<QList<VideoItem> >("QList<VideoItem>");
    qRegisterMetaType<QList<ChannelItem> >("QList<ChannelItem>");
    qRegisterMetaType<QList<PlaylistItem> >("QList<PlaylistItem>");
    qRegisterMetaType<QList<VideoComment> >("QList<VideoComment>");
    qRegisterMetaType<QList<SponsorSegment> >("QList<SponsorSegment>");
    qRegisterMetaType<LyricLine>("LyricLine");
    qRegisterMetaType<QList<LyricLine> >("QList<LyricLine>");

    // Decided once, up front: everything gated on the Windows version asks
    // this rather than testing for itself.
    OsCapabilities::probe();

    application.setStyleSheet(SightlineStyle::sheet());

    MainWindow window;
    QString error;
    if (!window.initialise(&error)) {
        QMessageBox::critical(0, QString::fromLatin1("Sightline"),
            QString::fromUtf8("No se pudo iniciar:\n\n") + error);
        return 1;
    }

    window.show();
    return application.exec();
}
