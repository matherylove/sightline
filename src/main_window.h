#ifndef SIGHTLINE_MAIN_WINDOW_H
#define SIGHTLINE_MAIN_WINDOW_H

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

#include "app_settings.h"
#include "playback.h"
#include "media_types.h"
#include "sightline_paths.h"

class QLabel;
class QLineEdit;
class QMenuBar;
class QScrollArea;
class QStackedWidget;
class QTimer;

class Library;
class ListeningStats;
class MusicPage;
class PipWindow;
class PlaybackController;
class PlayerPage;
class Sidebar;
class SightlineStatusBar;
class SightlineTitleBar;
class SponsorBlock;
class StatsPage;
class VideoGrid;
class YtDlp;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    bool initialise(QString *error);

public slots:
    void formatRowActivated(const QString &itag);

protected:
    void closeEvent(QCloseEvent *event);
    void keyPressEvent(QKeyEvent *event);

private slots:
    void onSidebarActivated(const QString &key);
    void onSearchSubmitted();
    void onRefreshClicked();
    void onVideoActivated(const QString &videoId);
    void onVideoContextRequested(const QString &videoId, const QPoint &globalPos);

    void onVideoReady(const QString &token, const VideoItem &video);
    void onListReady(const QString &token, const QList<VideoItem> &videos);
    void onCommentsReady(const QString &token, const QList<VideoComment> &comments);
    void onExtractorFailed(const QString &token, const QString &message);
    void onExtractorProbed();
    void onExtractorBusy(int active, int queued);

    void onSegmentsReady(const QString &videoId, const QList<SponsorSegment> &segments);
    void onSponsorBlockOnlineChanged(bool online);

    void onPlaybackStateChanged(PlaybackController::State state);
    void onPlaybackFinished();
    void onSecondPlayed();
    void onSegmentSkipped(SponsorSegment::Category category, double savedSeconds);
    void onSegmentPending(SponsorSegment::Category category, double start, double end);

    void onSubscribeToggled(bool subscribed);
    void onDownloadRequested();
    void onCommentsRequested();
    void onPipRequested();
    void onPipClosed();
    void onLibraryChanged();
    void onStatusTick();

    void onLinkAccount();
    void onImportCsv();
    void onOpenSponsorBlockSettings();
    void onOpenToolsSettings();
    void onClearHistory();
    void onAbout();

private:
    enum View { FeedView, HomeView, HistoryView, DownloadsView, PlaylistView,
                ChannelView, SearchView, PlayerView, MusicView, StatsView };

    void buildChrome();
    void buildMenu();
    void buildViews();
    void rebuildSidebar();
    void showView(View view, const QString &argument = QString());
    void refreshStatusBar();
    void playVideo(const QString &videoId, bool music);
    void updateWindowTitle(const QString &suffix);
    void saveEverything();

    SightlinePaths paths_;
    AppSettings settings_;
    SettingsStore *settingsStore_;
    AccountState account_;

    Library *library_;
    ListeningStats *stats_;
    YtDlp *extractor_;
    SponsorBlock *sponsorBlock_;
    PlaybackController *playback_;

    SightlineTitleBar *titleBar_;
    QMenuBar *menuBar_;
    Sidebar *sidebar_;
    QLineEdit *searchEdit_;
    QLabel *crumbLabel_;
    QLabel *crumbDetail_;
    QStackedWidget *stack_;
    SightlineStatusBar *statusBar_;

    QScrollArea *gridScroll_;
    VideoGrid *grid_;
    PlayerPage *player_;
    MusicPage *music_;
    StatsPage *statsPage_;
    PipWindow *pip_;

    View view_;
    QString viewArgument_;
    VideoItem currentVideo_;
    QString currentVideoId_;
    bool currentIsMusic_;

    // Tokens map a running yt-dlp job back to what asked for it, so a stale
    // result from a view the user has already left is discarded instead of
    // overwriting the screen.
    QString feedToken_;
    QString searchToken_;
    QString extractToken_;
    QString relatedToken_;
    QString channelToken_;
    QString commentsToken_;
    QStringList feedRefreshQueue_;

    QTimer *statusTimer_;
    QString lastError_;
};

#endif
