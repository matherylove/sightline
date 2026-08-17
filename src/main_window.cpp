#include "main_window.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <QProcess>
#include <QProcessEnvironment>

#include "dialogs.h"
#include "library.h"
#include "listening_stats.h"
#include "music_page.h"
#include "pip_window.h"
#include "playback.h"
#include "player_page.h"
#include "sightline_paint.h"
#include "sightline_style.h"
#include "sightline_window.h"
#include "sponsorblock.h"
#include "stats_page.h"
#include "widgets.h"
#include "ytdlp.h"

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent),
      settingsStore_(0), library_(0), stats_(0), extractor_(0),
      sponsorBlock_(0), playback_(0),
      titleBar_(0), menuBar_(0), sidebar_(0), searchEdit_(0),
      crumbLabel_(0), crumbDetail_(0), stack_(0), statusBar_(0),
      gridScroll_(0), grid_(0), player_(0), music_(0), statsPage_(0), pip_(0),
      view_(FeedView), currentIsMusic_(false), statusTimer_(0)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setObjectName(QString::fromLatin1("appRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QString::fromLatin1("Sightline"));
}

MainWindow::~MainWindow()
{
    delete settingsStore_;
}

bool MainWindow::initialise(QString *error)
{
    if (!paths_.initialize(error))
        return false;

    settingsStore_ = new SettingsStore(paths_);
    settings_ = settingsStore_->load();
    account_ = settingsStore_->loadAccount();

    library_ = new Library(paths_, this);
    library_->load();
    connect(library_, SIGNAL(changed()), this, SLOT(onLibraryChanged()));

    stats_ = new ListeningStats(paths_, this);
    stats_->load();

    extractor_ = new YtDlp(paths_, settings_, this);
    connect(extractor_, SIGNAL(videoReady(QString, VideoItem)),
            this, SLOT(onVideoReady(QString, VideoItem)));
    connect(extractor_, SIGNAL(listReady(QString, QList<VideoItem>)),
            this, SLOT(onListReady(QString, QList<VideoItem>)));
    connect(extractor_, SIGNAL(commentsReady(QString, QList<VideoComment>)),
            this, SLOT(onCommentsReady(QString, QList<VideoComment>)));
    connect(extractor_, SIGNAL(failed(QString, QString)),
            this, SLOT(onExtractorFailed(QString, QString)));
    connect(extractor_, SIGNAL(probed()), this, SLOT(onExtractorProbed()));
    connect(extractor_, SIGNAL(busyChanged(int, int)), this, SLOT(onExtractorBusy(int, int)));

    sponsorBlock_ = new SponsorBlock(paths_, settings_, this);
    connect(sponsorBlock_, SIGNAL(segmentsReady(QString, QList<SponsorSegment>)),
            this, SLOT(onSegmentsReady(QString, QList<SponsorSegment>)));
    connect(sponsorBlock_, SIGNAL(onlineChanged(bool)),
            this, SLOT(onSponsorBlockOnlineChanged(bool)));

    playback_ = new PlaybackController(this);
    playback_->applySettings(settings_);
    connect(playback_, SIGNAL(stateChanged(PlaybackController::State)),
            this, SLOT(onPlaybackStateChanged(PlaybackController::State)));
    connect(playback_, SIGNAL(finished()), this, SLOT(onPlaybackFinished()));
    connect(playback_, SIGNAL(secondPlayed()), this, SLOT(onSecondPlayed()));
    connect(playback_, SIGNAL(segmentSkipped(SponsorSegment::Category, double)),
            this, SLOT(onSegmentSkipped(SponsorSegment::Category, double)));
    connect(playback_, SIGNAL(segmentPending(SponsorSegment::Category, double, double)),
            this, SLOT(onSegmentPending(SponsorSegment::Category, double, double)));

    buildChrome();
    buildViews();
    rebuildSidebar();

    resize(settings_.windowWidth, settings_.windowHeight);

    statusTimer_ = new QTimer(this);
    connect(statusTimer_, SIGNAL(timeout()), this, SLOT(onStatusTick()));
    statusTimer_->start(4000);

    extractor_->probe();
    showView(FeedView);
    refreshStatusBar();
    return true;
}

void MainWindow::buildChrome()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    titleBar_ = new SightlineTitleBar(QString::fromLatin1("Sightline"), true, true, this);
    root->addWidget(titleBar_);

    buildMenu();
    root->addWidget(menuBar_);

    QWidget *body = new QWidget(this);
    QHBoxLayout *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    sidebar_ = new Sidebar(body);
    connect(sidebar_, SIGNAL(itemActivated(QString)), this, SLOT(onSidebarActivated(QString)));
    bodyLayout->addWidget(sidebar_);

    QWidget *main = new QWidget(body);
    QVBoxLayout *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- toolbar ----------------------------------------------------------
    QWidget *toolbar = new QWidget(main);
    toolbar->setObjectName(QString::fromLatin1("toolBar"));
    toolbar->setAttribute(Qt::WA_StyledBackground, true);
    toolbar->setFixedHeight(34);

    QHBoxLayout *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(8, 0, 8, 0);
    toolLayout->setSpacing(8);

    QPushButton *back = new QPushButton(QString::fromUtf8("\xE2\x97\x82"), toolbar);
    back->setFixedSize(24, 20);
    connect(back, SIGNAL(clicked()), this, SLOT(onRefreshClicked()));
    toolLayout->addWidget(back);

    QPushButton *forward = new QPushButton(QString::fromUtf8("\xE2\x96\xB8"), toolbar);
    forward->setFixedSize(24, 20);
    toolLayout->addWidget(forward);

    searchEdit_ = new QLineEdit(toolbar);
    searchEdit_->setPlaceholderText(QString::fromUtf8("Buscar en YouTube"));
    searchEdit_->setFixedHeight(20);
    searchEdit_->setMaximumWidth(340);
    connect(searchEdit_, SIGNAL(returnPressed()), this, SLOT(onSearchSubmitted()));
    toolLayout->addWidget(searchEdit_, 1);
    toolLayout->addStretch(1);

    QPushButton *refresh = new QPushButton(QString::fromUtf8("Actualizar"), toolbar);
    refresh->setObjectName(QString::fromLatin1("primaryButton"));
    refresh->setFixedHeight(20);
    connect(refresh, SIGNAL(clicked()), this, SLOT(onRefreshClicked()));
    toolLayout->addWidget(refresh);

    mainLayout->addWidget(toolbar);

    // --- breadcrumb -------------------------------------------------------
    QWidget *crumb = new QWidget(main);
    crumb->setObjectName(QString::fromLatin1("crumbBar"));
    crumb->setAttribute(Qt::WA_StyledBackground, true);
    crumb->setFixedHeight(26);

    QHBoxLayout *crumbLayout = new QHBoxLayout(crumb);
    crumbLayout->setContentsMargins(10, 0, 10, 0);
    crumbLayout->setSpacing(6);

    crumbLabel_ = new QLabel(crumb);
    crumbLabel_->setObjectName(QString::fromLatin1("crumbText"));
    crumbLayout->addWidget(crumbLabel_);

    crumbDetail_ = new QLabel(crumb);
    crumbDetail_->setObjectName(QString::fromLatin1("crumbStrong"));
    crumbLayout->addWidget(crumbDetail_);
    crumbLayout->addStretch(1);

    mainLayout->addWidget(crumb);

    stack_ = new QStackedWidget(main);
    mainLayout->addWidget(stack_, 1);

    statusBar_ = new SightlineStatusBar(main);
    mainLayout->addWidget(statusBar_);

    bodyLayout->addWidget(main, 1);
    root->addWidget(body, 1);
}

void MainWindow::buildMenu()
{
    menuBar_ = new QMenuBar(this);
    menuBar_->setObjectName(QString::fromLatin1("menuBar"));
    menuBar_->setFixedHeight(22);
    menuBar_->setNativeMenuBar(false);

    QMenu *file = menuBar_->addMenu(QString::fromUtf8("Archivo"));
    file->addAction(QString::fromUtf8("Abrir enlace…"), this, SLOT(onSearchSubmitted()));
    file->addSeparator();
    file->addAction(QString::fromUtf8("Salir"), this, SLOT(close()));

    QMenu *view = menuBar_->addMenu(QString::fromUtf8("Ver"));
    view->addAction(QString::fromUtf8("Suscripciones"))->setData(QString::fromLatin1("feed"));
    view->addAction(QString::fromUtf8("Historial"))->setData(QString::fromLatin1("history"));
    view->addAction(QString::fromUtf8("Mis estadísticas"))->setData(QString::fromLatin1("stats"));

    QMenu *library = menuBar_->addMenu(QString::fromUtf8("Biblioteca"));
    library->addAction(QString::fromUtf8("Vincular cuenta…"), this, SLOT(onLinkAccount()));
    library->addAction(QString::fromUtf8("Importar CSV de Takeout…"), this, SLOT(onImportCsv()));
    library->addSeparator();
    library->addAction(QString::fromUtf8("Borrar historial"), this, SLOT(onClearHistory()));

    QMenu *playback = menuBar_->addMenu(QString::fromUtf8("Reproducción"));
    playback->addAction(QString::fromUtf8("Reproducir / pausar"), playback_, SLOT(togglePause()));
    playback->addAction(QString::fromUtf8("Ventana flotante"), this, SLOT(onPipRequested()));

    QMenu *tools = menuBar_->addMenu(QString::fromUtf8("Herramientas"));
    tools->addAction(QString::fromUtf8("SponsorBlock…"), this, SLOT(onOpenSponsorBlockSettings()));
    tools->addAction(QString::fromUtf8("Cadena de extracción…"), this, SLOT(onOpenToolsSettings()));

    QMenu *help = menuBar_->addMenu(QString::fromUtf8("Ayuda"));
    help->addAction(QString::fromUtf8("Acerca de Sightline"), this, SLOT(onAbout()));
}

void MainWindow::buildViews()
{
    gridScroll_ = new QScrollArea(stack_);
    gridScroll_->setWidgetResizable(true);
    gridScroll_->setFrameShape(QFrame::NoFrame);
    gridScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    grid_ = new VideoGrid(library_, gridScroll_);
    connect(grid_, SIGNAL(videoActivated(QString)), this, SLOT(onVideoActivated(QString)));
    connect(grid_, SIGNAL(videoContextRequested(QString, QPoint)),
            this, SLOT(onVideoContextRequested(QString, QPoint)));
    gridScroll_->setWidget(grid_);
    stack_->addWidget(gridScroll_);

    player_ = new PlayerPage(library_, playback_, stack_);
    connect(player_, SIGNAL(playRequested(QString)), this, SLOT(onVideoActivated(QString)));
    connect(player_, SIGNAL(subscribeToggled(bool)), this, SLOT(onSubscribeToggled(bool)));
    connect(player_, SIGNAL(downloadRequested()), this, SLOT(onDownloadRequested()));
    connect(player_, SIGNAL(commentsRequested()), this, SLOT(onCommentsRequested()));
    connect(player_, SIGNAL(pipRequested()), this, SLOT(onPipRequested()));
    stack_->addWidget(player_);

    music_ = new MusicPage(library_, playback_, stack_);
    connect(music_, SIGNAL(trackActivated(QString)), this, SLOT(onVideoActivated(QString)));
    stack_->addWidget(music_);

    statsPage_ = new StatsPage(stats_, stack_);
    stack_->addWidget(statsPage_);
}

void MainWindow::rebuildSidebar()
{
    const QString selected = sidebar_->selectedKey();
    sidebar_->beginRebuild();

    sidebar_->addGroup(QString::fromUtf8("Biblioteca"));
    sidebar_->addItem(QString::fromUtf8("\xE2\x97\x87"), QString::fromUtf8("Inicio"),
                      QString::fromLatin1("home"));
    sidebar_->addItem(QString::fromUtf8("\xE2\x97\x88"), QString::fromUtf8("Suscripciones"),
                      QString::fromLatin1("feed"), library_->channels().size());
    sidebar_->addItem(QString::fromUtf8("\xE2\x97\xB7"), QString::fromUtf8("Historial"),
                      QString::fromLatin1("history"));
    sidebar_->addItem(QString::fromUtf8("\xE2\x96\xA4"), QString::fromUtf8("Descargas"),
                      QString::fromLatin1("downloads"));

    sidebar_->addGroup(QString::fromUtf8("Mis listas"));
    const QList<PlaylistItem> playlists = library_->playlists();
    for (int i = 0; i < playlists.size(); ++i) {
        sidebar_->addItem(QString::fromUtf8("\xE2\x96\xB8"), playlists.at(i).title,
                          QString::fromLatin1("playlist:") + playlists.at(i).id,
                          playlists.at(i).videoIds.size());
    }

    sidebar_->addGroup(QString::fromUtf8("Música"));
    sidebar_->addItem(QString::fromUtf8("\xE2\x99\xAA"), QString::fromUtf8("Reproducidas"),
                      QString::fromLatin1("music"));
    sidebar_->addItem(QString::fromUtf8("\xE2\x97\x94"), QString::fromUtf8("Mis estadísticas"),
                      QString::fromLatin1("stats"));

    const QList<ChannelItem> channels = library_->channels();
    if (!channels.isEmpty()) {
        sidebar_->addGroup(QString::fromUtf8("Canales"));
        for (int i = 0; i < channels.size() && i < 20; ++i) {
            const ChannelItem &channel = channels.at(i);
            sidebar_->addItem(QString::fromUtf8("\xE2\x97\x8B"), channel.name,
                              QString::fromLatin1("channel:") + channel.id,
                              channel.unwatchedCount > 0 ? channel.unwatchedCount : -1,
                              channel.unwatchedCount > 0);
        }
    }

    sidebar_->endRebuild();
    sidebar_->setSelectedKey(selected);
}

void MainWindow::showView(View view, const QString &argument)
{
    view_ = view;
    viewArgument_ = argument;

    switch (view) {
    case FeedView: {
        stack_->setCurrentWidget(gridScroll_);
        sidebar_->setSelectedKey(QString::fromLatin1("feed"));
        crumbLabel_->setText(QString::fromUtf8("Suscripciones \xE2\x80\xBA"));
        crumbDetail_->setText(QString::fromUtf8("Novedades"));
        const QList<VideoItem> feed = library_->subscriptionFeed(60);
        grid_->setEmptyText(library_->channels().isEmpty()
            ? QString::fromUtf8("Sin suscripciones todavía.\n\n"
                                "Biblioteca → Vincular cuenta, o importa el CSV de Takeout.")
            : QString::fromUtf8("Pulsa Actualizar para traer los últimos vídeos."));
        grid_->setVideos(feed);
        updateWindowTitle(QString::fromUtf8("Suscripciones"));
        break;
    }
    case HomeView:
        stack_->setCurrentWidget(gridScroll_);
        crumbLabel_->setText(QString::fromUtf8("Inicio \xE2\x80\xBA"));
        crumbDetail_->setText(QString::fromUtf8("Continuar viendo"));
        grid_->setEmptyText(QString::fromUtf8("Nada empezado todavía."));
        grid_->setVideos(library_->history(20));
        updateWindowTitle(QString::fromUtf8("Inicio"));
        break;

    case HistoryView:
        stack_->setCurrentWidget(gridScroll_);
        crumbLabel_->setText(QString::fromUtf8("Historial \xE2\x80\xBA"));
        crumbDetail_->setText(QString::fromUtf8("Todo lo visto en este equipo"));
        grid_->setEmptyText(QString::fromUtf8("El historial está vacío."));
        grid_->setVideos(library_->history(100));
        updateWindowTitle(QString::fromUtf8("Historial"));
        break;

    case DownloadsView:
        stack_->setCurrentWidget(gridScroll_);
        crumbLabel_->setText(QString::fromUtf8("Descargas \xE2\x80\xBA"));
        crumbDetail_->setText(settings_.downloadDirectory);
        grid_->setEmptyText(QString::fromUtf8("Sin descargas todavía."));
        grid_->setVideos(QList<VideoItem>());
        updateWindowTitle(QString::fromUtf8("Descargas"));
        break;

    case PlaylistView: {
        stack_->setCurrentWidget(gridScroll_);
        const PlaylistItem playlist = library_->playlist(argument);
        crumbLabel_->setText(QString::fromUtf8("Lista \xE2\x80\xBA"));
        crumbDetail_->setText(playlist.title);
        QList<VideoItem> videos;
        for (int i = 0; i < playlist.videoIds.size(); ++i) {
            const VideoItem video = library_->remembered(playlist.videoIds.at(i));
            if (!video.id.isEmpty())
                videos.append(video);
        }
        grid_->setEmptyText(QString::fromUtf8("Esta lista está vacía."));
        grid_->setVideos(videos);
        updateWindowTitle(playlist.title);
        break;
    }
    case ChannelView: {
        stack_->setCurrentWidget(gridScroll_);
        const ChannelItem channel = library_->channel(argument);
        crumbLabel_->setText(QString::fromUtf8("Canal \xE2\x80\xBA"));
        crumbDetail_->setText(channel.name);
        grid_->setEmptyText(QString::fromUtf8("Cargando vídeos del canal…"));
        channelToken_ = extractor_->channelFeed(argument, 30);
        updateWindowTitle(channel.name);
        break;
    }
    case SearchView:
        stack_->setCurrentWidget(gridScroll_);
        crumbLabel_->setText(QString::fromUtf8("Búsqueda \xE2\x80\xBA"));
        crumbDetail_->setText(argument);
        grid_->setEmptyText(QString::fromUtf8("Buscando…"));
        updateWindowTitle(QString::fromUtf8("Búsqueda: ") + argument);
        break;

    case PlayerView:
        stack_->setCurrentWidget(player_);
        crumbLabel_->setText(QString::fromUtf8("Reproduciendo \xE2\x80\xBA"));
        crumbDetail_->setText(currentVideo_.title);
        break;

    case MusicView:
        stack_->setCurrentWidget(music_);
        sidebar_->setSelectedKey(QString::fromLatin1("music"));
        crumbLabel_->setText(QString::fromUtf8("Música \xE2\x80\xBA"));
        crumbDetail_->setText(QString::fromUtf8("Reproducidas"));
        music_->setAlbum(QString::fromUtf8("Reproducidas recientemente"),
                         QString::fromUtf8("Desde tu historial local"),
                         library_->history(30));
        updateWindowTitle(QString::fromUtf8("Música"));
        break;

    case StatsView:
        stack_->setCurrentWidget(statsPage_);
        sidebar_->setSelectedKey(QString::fromLatin1("stats"));
        crumbLabel_->setText(QString::fromUtf8("Música \xE2\x80\xBA"));
        crumbDetail_->setText(QString::fromUtf8("Mis estadísticas"));
        statsPage_->refresh();
        updateWindowTitle(QString::fromUtf8("Mis estadísticas"));
        break;
    }
}

void MainWindow::updateWindowTitle(const QString &suffix)
{
    titleBar_->setSuffix(suffix);
    setWindowTitle(QString::fromLatin1("Sightline — ") + suffix);
}

void MainWindow::onSidebarActivated(const QString &key)
{
    if (key == QLatin1String("home"))            showView(HomeView);
    else if (key == QLatin1String("feed"))       showView(FeedView);
    else if (key == QLatin1String("history"))    showView(HistoryView);
    else if (key == QLatin1String("downloads"))  showView(DownloadsView);
    else if (key == QLatin1String("music"))      showView(MusicView);
    else if (key == QLatin1String("stats"))      showView(StatsView);
    else if (key.startsWith(QLatin1String("playlist:")))
        showView(PlaylistView, key.mid(9));
    else if (key.startsWith(QLatin1String("channel:")))
        showView(ChannelView, key.mid(8));
}

void MainWindow::onSearchSubmitted()
{
    const QString query = searchEdit_->text().trimmed();
    if (query.isEmpty())
        return;

    // A pasted watch URL goes straight to the player instead of being
    // searched for, which is what anyone pasting a link expects.
    if (query.contains(QLatin1String("youtube.com/watch"))
        || query.contains(QLatin1String("youtu.be/"))) {
        QString id;
        const int marker = query.indexOf(QLatin1String("v="));
        if (marker >= 0)
            id = query.mid(marker + 2, 11);
        else {
            const int slash = query.lastIndexOf(QLatin1Char('/'));
            if (slash >= 0)
                id = query.mid(slash + 1, 11);
        }
        if (id.size() == 11) {
            onVideoActivated(id);
            return;
        }
    }

    if (!searchToken_.isEmpty())
        extractor_->cancel(searchToken_);
    searchToken_ = extractor_->search(query, 24);
    showView(SearchView, query);
}

void MainWindow::onRefreshClicked()
{
    if (view_ == FeedView) {
        // One process per channel would be dozens of processes; they are
        // queued and the extractor's own cap keeps two running at a time.
        feedRefreshQueue_.clear();
        const QList<ChannelItem> channels = library_->channels();
        for (int i = 0; i < channels.size(); ++i)
            feedRefreshQueue_.append(channels.at(i).id);
        if (!feedRefreshQueue_.isEmpty()) {
            const QString channelId = feedRefreshQueue_.takeFirst();
            feedToken_ = extractor_->channelFeed(channelId, 12);
            feedToken_ += QString::fromLatin1("|") + channelId;
        }
    } else {
        showView(view_, viewArgument_);
    }
    refreshStatusBar();
}

void MainWindow::onVideoActivated(const QString &videoId)
{
    playVideo(videoId, view_ == MusicView);
}

void MainWindow::playVideo(const QString &videoId, bool music)
{
    if (videoId.isEmpty())
        return;

    currentVideoId_ = videoId;
    currentIsMusic_ = music;

    VideoItem video = library_->remembered(videoId);
    if (video.id.isEmpty()) {
        video.id = videoId;
        video.title = QString::fromUtf8("Resolviendo…");
    }
    currentVideo_ = video;

    if (!music) {
        player_->setVideo(video);
        player_->setSubscribed(library_->isSubscribed(video.channelId));
        showView(PlayerView);
    } else {
        music_->setNowPlaying(video);
        showView(MusicView);
    }

    // A cached extraction is only good for about five hours; past that the
    // googlevideo URLs answer 403 and the video has to be resolved again.
    if (!video.detailed || video.urlsLikelyExpired()) {
        if (!extractToken_.isEmpty())
            extractor_->cancel(extractToken_);
        extractToken_ = extractor_->extract(videoId);
        statusBar_->setState(SightlineStatusBar::Working, QString::fromUtf8("Resolviendo"));
    } else {
        onVideoReady(QString(), video);
    }

    if (settings_.sponsorBlockEnabled && settings_.prefetchSegments)
        sponsorBlock_->fetch(videoId);

    if (!relatedToken_.isEmpty())
        extractor_->cancel(relatedToken_);
    relatedToken_ = extractor_->related(videoId, 10);
}

void MainWindow::onVideoReady(const QString &token, const VideoItem &video)
{
    if (!token.isEmpty() && token != extractToken_)
        return;
    extractToken_.clear();

    library_->remember(video);
    currentVideo_ = library_->remembered(video.id);

    const MediaFormat *videoFormat = currentVideo_.bestVideoFormat(
        settings_.maxHeight, settings_.avcOnly);
    const MediaFormat *audioFormat = currentVideo_.bestAudioFormat(settings_.preferAacAudio);

    if (!videoFormat && !audioFormat) {
        statusBar_->setState(SightlineStatusBar::Degraded,
                             QString::fromUtf8("Sin formatos"));
        lastError_ = QString::fromUtf8(
            "yt-dlp no devolvió formatos utilizables para este vídeo.");
        refreshStatusBar();
        return;
    }

    if (currentIsMusic_) {
        music_->setNowPlaying(currentVideo_);
        videoFormat = 0;
    } else {
        player_->setVideo(currentVideo_);
        player_->setSubscribed(library_->isSubscribed(currentVideo_.channelId));
        player_->setUrlExpiry(QString::fromUtf8(
            "La URL caduca en unas 5 h.\nSightline vuelve a pedirla sola si recibe un 403."));
    }

    const qint64 resume = settings_.rememberPosition
        ? library_->resumePosition(currentVideo_.id) : 0;
    playback_->open(currentVideo_, videoFormat, audioFormat, double(resume));

    if (settings_.trackListening)
        stats_->beginPlay(currentVideo_, currentIsMusic_);

    crumbDetail_->setText(currentVideo_.title);
    updateWindowTitle(currentVideo_.title);
    refreshStatusBar();
}

void MainWindow::onListReady(const QString &token, const QList<VideoItem> &videos)
{
    for (int i = 0; i < videos.size(); ++i)
        library_->remember(videos.at(i));

    if (token == searchToken_) {
        searchToken_.clear();
        grid_->setEmptyText(QString::fromUtf8("Sin resultados."));
        grid_->setVideos(videos);
        return;
    }

    if (token == channelToken_) {
        channelToken_.clear();
        library_->mergeChannelVideos(viewArgument_, videos);
        grid_->setVideos(videos);
        return;
    }

    if (token == relatedToken_) {
        relatedToken_.clear();

        // The mix playlist leads with the video itself, which is never a
        // useful recommendation, so it is dropped.
        QList<VideoItem> filtered;
        QList<VideoItem> sameChannel;
        for (int i = 0; i < videos.size(); ++i) {
            if (videos.at(i).id == currentVideoId_)
                continue;
            if (!currentVideo_.channelId.isEmpty()
                && videos.at(i).channelId == currentVideo_.channelId)
                sameChannel.append(videos.at(i));
            else
                filtered.append(videos.at(i));
        }
        player_->setRecommendations(filtered, sameChannel);
        return;
    }

    if (feedToken_.startsWith(token) && !token.isEmpty()) {
        const int marker = feedToken_.indexOf(QLatin1Char('|'));
        const QString channelId = marker >= 0 ? feedToken_.mid(marker + 1) : QString();
        feedToken_.clear();
        if (!channelId.isEmpty())
            library_->mergeChannelVideos(channelId, videos);

        if (!feedRefreshQueue_.isEmpty()) {
            const QString next = feedRefreshQueue_.takeFirst();
            feedToken_ = extractor_->channelFeed(next, 12);
            feedToken_ += QString::fromLatin1("|") + next;
        }
        if (view_ == FeedView)
            grid_->setVideos(library_->subscriptionFeed(60));
        return;
    }
}

void MainWindow::onCommentsReady(const QString &token, const QList<VideoComment> &comments)
{
    if (token != commentsToken_)
        return;
    commentsToken_.clear();
    player_->setComments(comments);
}

void MainWindow::onCommentsRequested()
{
    if (currentVideoId_.isEmpty())
        return;
    if (!commentsToken_.isEmpty())
        extractor_->cancel(commentsToken_);
    commentsToken_ = extractor_->comments(currentVideoId_, 40);
}

void MainWindow::onExtractorFailed(const QString &token, const QString &message)
{
    Q_UNUSED(token);
    lastError_ = message;
    statusBar_->setState(SightlineStatusBar::Degraded, QString::fromUtf8("Error"));
    refreshStatusBar();
}

void MainWindow::onExtractorProbed()
{
    refreshStatusBar();
}

void MainWindow::onExtractorBusy(int active, int queued)
{
    if (active > 0) {
        statusBar_->setState(SightlineStatusBar::Working,
            queued > 0 ? QString::fromUtf8("Trabajando (%1)").arg(queued + active)
                       : QString::fromUtf8("Trabajando"));
    } else if (extractor_->chainState() == YtDlp::ChainReady) {
        statusBar_->setState(SightlineStatusBar::Ready, QString::fromUtf8("Listo"));
    }
    refreshStatusBar();
}

void MainWindow::onSegmentsReady(const QString &videoId, const QList<SponsorSegment> &segments)
{
    if (videoId != currentVideoId_)
        return;
    playback_->setSegments(segments);
    player_->setSegments(segments);
    if (pip_)
        pip_->setSegments(segments);
    refreshStatusBar();
}

void MainWindow::onSponsorBlockOnlineChanged(bool)
{
    refreshStatusBar();
}

void MainWindow::onSegmentSkipped(SponsorSegment::Category category, double savedSeconds)
{
    const SponsorSegment::Category kind = category;
    QColor colour;
    switch (kind) {
    case SponsorSegment::Sponsor:       colour = SightlineStyle::sbSponsor(); break;
    case SponsorSegment::Intro:         colour = SightlineStyle::sbIntro(); break;
    case SponsorSegment::SelfPromo:     colour = SightlineStyle::sbPromo(); break;
    case SponsorSegment::Interaction:   colour = SightlineStyle::sbInter(); break;
    case SponsorSegment::MusicOffTopic: colour = SightlineStyle::sbMusic(); break;
    default:                            colour = SightlineStyle::amber(); break;
    }

    const QString detail = QString::fromUtf8("Omitidos %1 s").arg(qRound(savedSeconds));
    player_->surface()->setSkipToast(
        SponsorSegment::displayName(kind).toUpper(), detail, colour);
    if (pip_)
        pip_->surface()->setSkipToast(
            SponsorSegment::displayName(kind).toUpper(), detail, colour);

    if (settings_.countTimeSaved) {
        settings_.secondsSaved += qint64(savedSeconds);
        settingsStore_->save(settings_);
    }
    refreshStatusBar();
}

void MainWindow::onSegmentPending(SponsorSegment::Category category, double start, double end)
{
    Q_UNUSED(start);
    const SponsorSegment::Category kind = category;
    player_->surface()->setSkipToast(
        SponsorSegment::displayName(kind).toUpper(),
        QString::fromUtf8("Saltar hasta %1").arg(SightlinePaint::clockLabel(end)),
        SightlineStyle::teal());
}

void MainWindow::onPlaybackStateChanged(PlaybackController::State state)
{
    Q_UNUSED(state);
    refreshStatusBar();
}

void MainWindow::onPlaybackFinished()
{
    if (settings_.rememberPosition)
        library_->recordWatch(currentVideo_, qint64(playback_->duration()));
    if (settings_.trackListening)
        stats_->endPlay();

    const QString next = player_->nextVideoId();
    if (settings_.autoplayNext && !next.isEmpty())
        playVideo(next, currentIsMusic_);
}

void MainWindow::onSecondPlayed()
{
    if (settings_.trackListening)
        stats_->addPlayedSeconds(1);
}

void MainWindow::onSubscribeToggled(bool subscribed)
{
    if (currentVideo_.channelId.isEmpty())
        return;

    if (subscribed) {
        ChannelItem channel = library_->channel(currentVideo_.channelId);
        if (channel.id.isEmpty()) {
            channel.id = currentVideo_.channelId;
            channel.name = currentVideo_.channelName;
        }
        library_->subscribe(channel);
    } else {
        library_->unsubscribe(currentVideo_.channelId);
    }
    library_->save();
}

void MainWindow::onDownloadRequested()
{
    if (currentVideo_.id.isEmpty() || !currentVideo_.detailed) {
        SightlineDialog::showMessage(this, QString::fromUtf8("Descargar"),
            QString::fromUtf8("Todavía no se han resuelto los formatos de este vídeo."));
        return;
    }

    DownloadDialog dialog(currentVideo_, settings_,
                          sponsorBlock_->segments(currentVideo_.id), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QStringList arguments = dialog.buildArguments(
        paths_.cache() + QString::fromLatin1("/ytdlp"));

    // The download runs as its own detached process so it survives the app
    // being closed, which is what anyone downloading a long video wants.
    QProcess *process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QString::fromLatin1("PYTHONIOENCODING"), QString::fromLatin1("utf-8"));
    process->setProcessEnvironment(env);
    process->start(extractor_->binaryPath(), arguments);

    statusBar_->setGrowCell(QString::fromUtf8("Descargando: ") + currentVideo_.title);
}

void MainWindow::onVideoContextRequested(const QString &videoId, const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *play = menu.addAction(QString::fromUtf8("Reproducir"));
    QAction *watchLater = menu.addAction(QString::fromUtf8("Añadir a «Ver más tarde»"));
    QAction *liked = menu.addAction(QString::fromUtf8("Añadir a «Me gusta»"));
    menu.addSeparator();
    QAction *download = menu.addAction(QString::fromUtf8("Descargar…"));

    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == play) {
        onVideoActivated(videoId);
    } else if (chosen == watchLater) {
        library_->addToPlaylist(Library::watchLaterId(), videoId);
        library_->save();
    } else if (chosen == liked) {
        library_->addToPlaylist(Library::likedId(), videoId);
        library_->save();
    } else if (chosen == download) {
        onVideoActivated(videoId);
        onDownloadRequested();
    }
}

void MainWindow::onPipRequested()
{
    if (!pip_) {
        pip_ = new PipWindow(playback_, 0);
        connect(pip_, SIGNAL(returnToWindowRequested()), this, SLOT(onPipClosed()));
    }
    pip_->setVideo(currentVideo_);
    pip_->setSegments(playback_->segments());
    pip_->show();
    pip_->raise();
}

void MainWindow::onPipClosed()
{
    if (pip_)
        pip_->hide();
    showView(PlayerView);
}

void MainWindow::onLibraryChanged()
{
    rebuildSidebar();
}

void MainWindow::onStatusTick()
{
    refreshStatusBar();
}

void MainWindow::formatRowActivated(const QString &itag)
{
    const MediaFormat *format = currentVideo_.formatByItag(itag);
    if (!format)
        return;
    const MediaFormat *audio = currentVideo_.bestAudioFormat(settings_.preferAacAudio);
    playback_->open(currentVideo_, format, audio, playback_->position());
}

void MainWindow::refreshStatusBar()
{
    const YtDlp::ChainState chain = extractor_->chainState();
    const bool degraded = (chain != YtDlp::ChainReady);

    statusBar_->setCell(0, extractor_->version().isEmpty()
        ? QString::fromUtf8("yt-dlp sin detectar")
        : QString::fromUtf8("yt-dlp %1").arg(extractor_->version()),
        chain == YtDlp::ChainMissingBinary ? SightlineStatusBar::Degraded
                                           : SightlineStatusBar::Ready);

    statusBar_->setCell(1,
        extractor_->hasJsRuntime() ? QString::fromUtf8("qjs ok")
                                   : QString::fromUtf8("qjs no encontrado"),
        extractor_->hasJsRuntime() ? SightlineStatusBar::Working
                                   : SightlineStatusBar::Degraded);

    statusBar_->setCell(2,
        settings_.potProviderUrl.isEmpty() ? QString::fromUtf8("POT sin proveedor")
                                           : QString::fromUtf8("POT lan"),
        settings_.potProviderUrl.isEmpty() ? SightlineStatusBar::Degraded
                                           : SightlineStatusBar::Working);

    statusBar_->setCell(3, QString::fromUtf8("SB %1").arg(sponsorBlock_->statusText()),
        sponsorBlock_->online() ? SightlineStatusBar::Working : SightlineStatusBar::Degraded);

    QString detail;
    if (!lastError_.isEmpty()) {
        detail = lastError_;
    } else if (playback_->isPlaying()) {
        detail = QString::fromUtf8("%1 \xC2\xB7 %2 \xC2\xB7 b\xC3\xBA""fer %3 s")
            .arg(playback_->videoCodecLabel())
            .arg(playback_->resolutionLabel())
            .arg(QString::number(qMax(0.0, playback_->buffered() - playback_->position()), 'f', 1));
    } else if (degraded) {
        detail = extractor_->chainSummary();
    } else {
        detail = QString::fromUtf8("Biblioteca: %1 canales \xC2\xB7 %2 listas")
            .arg(library_->channels().size())
            .arg(library_->playlists().size());
    }
    statusBar_->setGrowCell(detail);

    if (view_ == StatsView || currentIsMusic_) {
        statusBar_->setTrailing(QString::fromUtf8("Nunca sale del equipo"));
    } else {
        statusBar_->setTrailing(QString::fromUtf8("%1 seg. guardados")
            .arg(sponsorBlock_->cachedSegmentCount()));
    }

    if (degraded && !playback_->isPlaying())
        statusBar_->setState(SightlineStatusBar::Degraded, QString::fromUtf8("Degradado"));
}

void MainWindow::onLinkAccount()
{
    LinkAccountDialog dialog(this);
    connect(&dialog, SIGNAL(importCsvRequested()), this, SLOT(onImportCsv()));

    // The Device Flow needs a Google client id, which each build has to
    // register for itself; without one the dialog explains the CSV route
    // rather than showing a code that could never work.
    dialog.setUserCode(QString::fromLatin1("--------"));
    dialog.setStatus(QString::fromUtf8(
        "Configura un ID de cliente OAuth en config/account.json para usar esta vía. "
        "Mientras tanto, la importación por CSV de Takeout funciona sin registrar nada."));
    dialog.exec();
}

void MainWindow::onImportCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("Selecciona el CSV de suscripciones de Takeout"),
        QString(), QString::fromUtf8("Archivos CSV (*.csv);;Todos los archivos (*)"));
    if (path.isEmpty())
        return;

    QString error;
    const int imported = library_->importSubscriptionsCsv(path, &error);
    if (imported == 0 && !error.isEmpty()) {
        SightlineDialog::showMessage(this, QString::fromUtf8("Importar"), error);
        return;
    }

    library_->save();
    rebuildSidebar();
    SightlineDialog::showMessage(this, QString::fromUtf8("Importar"),
        QString::fromUtf8("Se importaron %1 suscripciones nuevas.\n\n"
                          "Pulsa Actualizar para traer sus últimos vídeos.").arg(imported));
}

void MainWindow::onOpenSponsorBlockSettings()
{
    SponsorBlockDialog dialog(settings_, sponsorBlock_->cachedSegmentCount(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings_ = dialog.result();
    settingsStore_->save(settings_);
    sponsorBlock_->applySettings(settings_);
    playback_->applySettings(settings_);
    refreshStatusBar();
}

void MainWindow::onOpenToolsSettings()
{
    ToolsDialog dialog(settings_, extractor_->binaryPath(), extractor_->jsRuntimePath(),
                       extractor_->version(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings_ = dialog.result();
    settingsStore_->save(settings_);
    extractor_->applySettings(settings_);
    extractor_->probe();
    refreshStatusBar();
}

void MainWindow::onClearHistory()
{
    if (!SightlineDialog::confirm(this, QString::fromUtf8("Borrar historial"),
            QString::fromUtf8("Se borrará todo el historial y las posiciones guardadas "
                              "de este equipo. Las estadísticas de escucha no se tocan."),
            QString::fromUtf8("Borrar")))
        return;
    library_->clearHistory();
    library_->save();
    showView(view_, viewArgument_);
}

void MainWindow::onAbout()
{
    SightlineDialog::showMessage(this, QString::fromUtf8("Acerca de Sightline"),
        QString::fromUtf8(
            "Sightline 0.1\n\n"
            "Cliente nativo de YouTube y YouTube Music para Windows XP.\n"
            "Qt 5.6.3 · MSVC 2017 v141_xp · Direct3D 9 · H.264\n\n"
            "La extracción la hace yt-dlp.exe aparte: se actualiza reemplazando "
            "su carpeta, sin recompilar nada.\n\n"
            "Suscripciones, listas e historial viven solo en este equipo."));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        playback_->togglePause();
        return;
    case Qt::Key_Left:
        playback_->step(-5.0);
        return;
    case Qt::Key_Right:
        playback_->step(5.0);
        return;
    case Qt::Key_Escape:
        if (view_ == PlayerView)
            showView(FeedView);
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void MainWindow::saveEverything()
{
    if (playback_->position() > 5.0 && !currentVideo_.id.isEmpty())
        library_->recordWatch(currentVideo_, qint64(playback_->position()));

    stats_->endPlay();
    stats_->save();
    library_->save();

    settings_.windowWidth = width();
    settings_.windowHeight = height();
    settings_.volume = playback_->volume();
    settingsStore_->save(settings_);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveEverything();
    if (pip_)
        pip_->close();
    extractor_->cancelAll();
    event->accept();
}
