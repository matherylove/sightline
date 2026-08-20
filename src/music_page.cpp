#include "music_page.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "library.h"
#include "playback.h"
#include "sightline_paint.h"
#include "sightline_style.h"
#include "widgets.h"

// An artwork tile. Falls back to the seeded hatch pattern until the image
// arrives, so the layout never jumps and an empty tile still reads as
// "loading" rather than as "broken".
class AlbumTile : public QWidget
{
public:
    explicit AlbumTile(int side, QWidget *parent = 0) : QWidget(parent)
    {
        setFixedSize(side, side);
    }

    void setArtwork(const QPixmap &artwork, const QString &seed)
    {
        artwork_ = artwork;
        seed_ = seed;
        update();
    }

protected:
    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        SightlinePaint::drawArtwork(painter, rect(), artwork_, seed_);
        SightlinePaint::drawFrame(painter, rect(), SightlineStyle::line());
    }

private:
    QPixmap artwork_;
    QString seed_;
};

// ================================================================= TrackRow

TrackRow::TrackRow(QWidget *parent)
    : QWidget(parent), index_(0), playing_(false), hovered_(false)
{
    setFixedHeight(32);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void TrackRow::setTrack(int index, const VideoItem &video, bool playing)
{
    index_ = index;
    video_ = video;
    playing_ = playing;
    setToolTip(video.title);
    update();
}

void TrackRow::setArtwork(const QPixmap &artwork)
{
    artwork_ = artwork;
    update();
}

void TrackRow::enterEvent(QEvent *event) { hovered_ = true; update(); QWidget::enterEvent(event); }
void TrackRow::leaveEvent(QEvent *event) { hovered_ = false; update(); QWidget::leaveEvent(event); }

void TrackRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        emit activated(video_.id);
}

void TrackRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    if (playing_) {
        QColor tint = SightlineStyle::teal();
        tint.setAlpha(26);
        painter.fillRect(rect(), tint);
        painter.fillRect(QRect(0, 0, 2, height()), SightlineStyle::teal());
    } else if (hovered_) {
        painter.fillRect(rect(), QColor(255, 255, 255, 8));
    }

    painter.setPen(QColor(SightlineStyle::line().red(), SightlineStyle::line().green(),
                          SightlineStyle::line().blue(), 107));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    painter.setFont(SightlinePaint::monoFont(10));
    painter.setPen(playing_ ? SightlineStyle::teal() : SightlineStyle::faint());
    painter.drawText(QRect(10, 0, 16, height()), Qt::AlignVCenter | Qt::AlignRight,
                     playing_ ? QString::fromUtf8("\xE2\x96\xB8") : QString::number(index_));

    // Cropped square out of the 16:9 thumbnail: album art is square and a
    // letterboxed strip here looks like a mistake.
    const QRect art(32, 2, 28, 28);
    SightlinePaint::drawArtwork(painter, art, artwork_, video_.id);
    SightlinePaint::drawFrame(painter, art, SightlineStyle::line());

    const int durationWidth = 44;
    const int artistWidth = 120;
    const int titleLeft = 68;
    const int titleWidth = qMax(60, width() - titleLeft - artistWidth - durationWidth - 24);

    painter.setFont(SightlinePaint::uiFont(11));
    painter.setPen(SightlineStyle::text());
    QFontMetrics metrics(painter.font());
    painter.drawText(QRect(titleLeft, 0, titleWidth, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     metrics.elidedText(video_.title, Qt::ElideRight, titleWidth));

    painter.setFont(SightlinePaint::uiFont(10));
    painter.setPen(SightlineStyle::dim());
    metrics = QFontMetrics(painter.font());
    painter.drawText(QRect(titleLeft + titleWidth + 12, 0, artistWidth, height()),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     metrics.elidedText(video_.channelName, Qt::ElideRight, artistWidth));

    painter.setFont(SightlinePaint::monoFont(10));
    painter.setPen(SightlineStyle::faint());
    painter.drawText(QRect(width() - durationWidth - 12, 0, durationWidth, height()),
                     Qt::AlignVCenter | Qt::AlignRight, video_.durationLabel());
}

// =============================================================== LyricsView

LyricsView::LyricsView(QWidget *parent)
    : QWidget(parent), currentIndex_(-1), scrollOffset_(0), synced_(false)
{
    setCursor(Qt::PointingHandCursor);
}

void LyricsView::clear()
{
    lines_.clear();
    currentIndex_ = -1;
    synced_ = false;
    update();
}

void LyricsView::setLines(const QList<LyricLine> &lines)
{
    lines_ = lines;
    currentIndex_ = -1;
    synced_ = false;
    for (int i = 0; i < lines_.size(); ++i) {
        if (lines_.at(i).synced()) {
            synced_ = true;
            break;
        }
    }
    update();
}

void LyricsView::setPosition(double seconds)
{
    if (!synced_)
        return;

    int index = -1;
    for (int i = 0; i < lines_.size(); ++i) {
        if (lines_.at(i).synced() && lines_.at(i).time <= seconds)
            index = i;
        else if (lines_.at(i).synced())
            break;
    }
    if (index == currentIndex_)
        return;
    currentIndex_ = index;

    // Keep the current line four rows down so there is context above it and
    // the eye does not have to chase the top edge of the panel.
    scrollOffset_ = qMax(0, currentIndex_ - 4);
    update();
}

int LyricsView::lineAt(int y) const
{
    const int rowHeight = 21;
    const int index = scrollOffset_ + (y - 10) / rowHeight;
    return (index >= 0 && index < lines_.size()) ? index : -1;
}

void LyricsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int index = lineAt(event->pos().y());
    if (index >= 0 && lines_.at(index).synced())
        emit seekRequested(lines_.at(index).time);
}

void LyricsView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    if (lines_.isEmpty()) {
        painter.setFont(SightlinePaint::uiFont(11));
        painter.setPen(SightlineStyle::faint());
        painter.drawText(rect().adjusted(12, 16, -12, 0), Qt::AlignTop | Qt::AlignLeft,
                         QString::fromUtf8("Sin letra para esta pista."));
        return;
    }

    const int rowHeight = 21;
    int y = 10;

    for (int i = scrollOffset_; i < lines_.size(); ++i) {
        if (y > height())
            break;

        const LyricLine &line = lines_.at(i);
        QColor ink = SightlineStyle::dim();
        bool bold = false;

        if (synced_) {
            if (i < currentIndex_)      ink = SightlineStyle::faint();
            else if (i == currentIndex_) { ink = SightlineStyle::teal(); bold = true; }
            else if (i == currentIndex_ + 1) ink = SightlineStyle::text();
        } else {
            ink = SightlineStyle::text();
        }

        if (line.synced()) {
            painter.setFont(SightlinePaint::monoFont(9));
            painter.setPen(i == currentIndex_ ? SightlineStyle::teal() : SightlineStyle::faint());
            painter.drawText(QRect(12, y, 34, rowHeight), Qt::AlignTop | Qt::AlignLeft,
                             SightlinePaint::clockLabel(line.time));
        }

        painter.setFont(SightlinePaint::uiFont(11, bold));
        painter.setPen(ink);
        const QFontMetrics metrics(painter.font());
        const int textLeft = line.synced() ? 55 : 12;
        painter.drawText(QRect(textLeft, y, width() - textLeft - 12, rowHeight),
                         Qt::AlignTop | Qt::AlignLeft,
                         metrics.elidedText(line.text, Qt::ElideRight, width() - textLeft - 12));
        y += rowHeight;
    }
}

// ================================================================ MusicPage

MusicPage::MusicPage(Library *library, PlaybackController *playback, QWidget *parent)
    : QWidget(parent),
      library_(library), playback_(playback),
      albumTile_(0), nowTile_(0), playPause_(0),
      albumTitle_(0), albumSubtitle_(0), albumKind_(0), trackLayout_(0),
      lyrics_(0), lyricsPanel_(0), lyricsButton_(0), lyricsStatus_(0),
      nowTitle_(0), nowArtist_(0), nowTime_(0), nowSeek_(0)
{
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QWidget *main = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- album header -----------------------------------------------------
    QWidget *hero = new QWidget(main);
    hero->setStyleSheet(QString::fromLatin1("border-bottom: 1px solid #333E42;"));
    QHBoxLayout *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(14, 14, 14, 14);
    heroLayout->setSpacing(14);

    albumTile_ = new AlbumTile(104, hero);
    heroLayout->addWidget(albumTile_, 0, Qt::AlignTop);

    QWidget *meta = new QWidget(hero);
    QVBoxLayout *metaLayout = new QVBoxLayout(meta);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    metaLayout->setSpacing(5);

    albumKind_ = new QLabel(meta);
    albumKind_->setFont(SightlinePaint::capsFont(9));
    albumKind_->setObjectName(QString::fromLatin1("tealLabel"));
    metaLayout->addWidget(albumKind_);

    albumTitle_ = new QLabel(meta);
    albumTitle_->setFont(SightlinePaint::uiFont(17, true));
    metaLayout->addWidget(albumTitle_);

    albumSubtitle_ = new QLabel(meta);
    albumSubtitle_->setObjectName(QString::fromLatin1("cardMeta"));
    metaLayout->addWidget(albumSubtitle_);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->setContentsMargins(0, 7, 0, 0);

    QPushButton *play = new QPushButton(QString::fromUtf8("Reproducir"), meta);
    play->setObjectName(QString::fromLatin1("primaryButton"));
    play->setFixedHeight(20);
    buttons->addWidget(play);

    QPushButton *shuffle = new QPushButton(QString::fromUtf8("Aleatorio"), meta);
    shuffle->setFixedHeight(20);
    connect(shuffle, SIGNAL(clicked()), this, SIGNAL(shuffleRequested()));
    buttons->addWidget(shuffle);

    QPushButton *download = new QPushButton(QString::fromUtf8("Descargar\xE2\x80\xA6"), meta);
    download->setFixedHeight(20);
    connect(download, SIGNAL(clicked()), this, SIGNAL(downloadRequested()));
    buttons->addWidget(download);

    lyricsButton_ = new QPushButton(QString::fromUtf8("Letras"), meta);
    lyricsButton_->setFixedHeight(20);
    lyricsButton_->setCheckable(true);
    lyricsButton_->setChecked(true);
    connect(lyricsButton_, SIGNAL(clicked()), this, SLOT(onLyricsToggled()));
    buttons->addWidget(lyricsButton_);

    buttons->addStretch(1);
    metaLayout->addLayout(buttons);
    metaLayout->addStretch(1);

    heroLayout->addWidget(meta, 1);
    hero->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    mainLayout->addWidget(hero);

    // --- track list -------------------------------------------------------
    QScrollArea *scroll = new QScrollArea(main);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *inner = new QWidget(scroll);
    trackLayout_ = new QVBoxLayout(inner);
    trackLayout_->setContentsMargins(0, 0, 0, 0);
    trackLayout_->setSpacing(0);
    trackLayout_->addStretch(1);

    // Top aligned inside the scroll area. Without this the list floated in
    // the middle of a very tall empty region, which is what the screenshot
    // showed: four rows adrift in a black field.
    inner->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    scroll->setWidget(inner);
    scroll->setAlignment(Qt::AlignTop);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mainLayout->addWidget(scroll, 1);

    // --- now playing bar --------------------------------------------------
    QWidget *nowBar = new QWidget(main);
    nowBar->setFixedHeight(44);
    nowBar->setObjectName(QString::fromLatin1("transport"));
    nowBar->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *nowLayout = new QHBoxLayout(nowBar);
    nowLayout->setContentsMargins(10, 0, 10, 0);
    nowLayout->setSpacing(10);

    nowTile_ = new AlbumTile(28, nowBar);
    nowLayout->addWidget(nowTile_);

    QWidget *text = new QWidget(nowBar);
    text->setFixedWidth(150);
    QVBoxLayout *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);

    nowTitle_ = new QLabel(text);
    nowTitle_->setObjectName(QString::fromLatin1("cardTitle"));
    textLayout->addWidget(nowTitle_);

    nowArtist_ = new QLabel(text);
    nowArtist_->setObjectName(QString::fromLatin1("cardMeta"));
    textLayout->addWidget(nowArtist_);
    nowLayout->addWidget(text);

    GlyphButton *previous = new GlyphButton(GlyphButton::Previous, nowBar);
    previous->setToolTip(QString::fromUtf8("Anterior"));
    nowLayout->addWidget(previous);

    playPause_ = new GlyphButton(GlyphButton::Pause, nowBar);
    playPause_->setAccent(true);
    connect(playPause_, SIGNAL(clicked()), playback_, SLOT(togglePause()));
    nowLayout->addWidget(playPause_);

    GlyphButton *next = new GlyphButton(GlyphButton::Next, nowBar);
    next->setToolTip(QString::fromUtf8("Siguiente"));
    nowLayout->addWidget(next);

    nowSeek_ = new SeekBar(nowBar);
    nowSeek_->setCompact(true);
    connect(nowSeek_, SIGNAL(seekRequested(double)), playback_, SLOT(seek(double)));
    nowLayout->addWidget(nowSeek_, 1);

    nowTime_ = new QLabel(QString::fromLatin1("00:00 / 00:00"), nowBar);
    nowTime_->setFont(SightlinePaint::monoFont(10));
    nowTime_->setObjectName(QString::fromLatin1("dimLabel"));
    nowLayout->addWidget(nowTime_);

    mainLayout->addWidget(nowBar);
    root->addWidget(main, 1);

    // --- lyrics panel -----------------------------------------------------
    lyricsPanel_ = new QWidget(this);
    lyricsPanel_->setObjectName(QString::fromLatin1("sidePane"));
    lyricsPanel_->setAttribute(Qt::WA_StyledBackground, true);
    lyricsPanel_->setFixedWidth(280);

    QVBoxLayout *lyricsLayout = new QVBoxLayout(lyricsPanel_);
    lyricsLayout->setContentsMargins(0, 0, 0, 0);
    lyricsLayout->setSpacing(0);

    QLabel *lyricsHead = new QLabel(QString::fromUtf8("Letra sincronizada"), lyricsPanel_);
    lyricsHead->setObjectName(QString::fromLatin1("paneHead"));
    lyricsHead->setFont(SightlinePaint::capsFont(9));
    lyricsHead->setFixedHeight(22);
    lyricsHead->setContentsMargins(10, 0, 10, 0);
    lyricsHead->setAttribute(Qt::WA_StyledBackground, true);
    lyricsLayout->addWidget(lyricsHead);

    lyrics_ = new LyricsView(lyricsPanel_);
    connect(lyrics_, SIGNAL(seekRequested(double)), playback_, SLOT(seek(double)));
    lyricsLayout->addWidget(lyrics_, 1);

    lyricsStatus_ = new QLabel(lyricsPanel_);
    lyricsStatus_->setFont(SightlinePaint::monoFont(9));
    lyricsStatus_->setObjectName(QString::fromLatin1("faintLabel"));
    lyricsStatus_->setWordWrap(true);
    lyricsStatus_->setContentsMargins(12, 8, 12, 8);
    lyricsStatus_->setStyleSheet(QString::fromLatin1("border-top: 1px solid #333E42;"));
    lyricsStatus_->setText(QString::fromUtf8(
        "Se buscan en LRCLIB al reproducir.\nSe guardan como .lrc en la caché."));
    lyricsLayout->addWidget(lyricsStatus_);

    root->addWidget(lyricsPanel_);

    connect(playback_, SIGNAL(positionChanged(double)), this, SLOT(onPositionChanged(double)));

    // Artwork arrives asynchronously from the fetcher thread; without this
    // the music view stays blank until something forces a rebuild.
    if (library_)
        connect(library_, SIGNAL(thumbnailReady(QString)), this, SLOT(onArtworkReady(QString)));
}

void MusicPage::onLyricsToggled()
{
    lyricsPanel_->setVisible(lyricsButton_->isChecked());
}

void MusicPage::setLyricsVisible(bool visible)
{
    lyricsButton_->setChecked(visible);
    lyricsPanel_->setVisible(visible);
}

void MusicPage::setAlbum(const QString &title, const QString &subtitle,
                         const QList<VideoItem> &tracks)
{
    albumKind_->setText(QString::fromUtf8("%1 pistas").arg(tracks.size()));
    albumTitle_->setText(title);
    albumSubtitle_->setText(subtitle);
    tracks_ = tracks;

    while (trackRows_.size() > tracks.size()) {
        trackRows_.last()->deleteLater();
        trackRows_.removeLast();
    }
    QWidget *inner = trackLayout_->parentWidget();
    while (trackRows_.size() < tracks.size()) {
        TrackRow *row = new TrackRow(inner);
        connect(row, SIGNAL(activated(QString)), this, SIGNAL(trackActivated(QString)));
        trackLayout_->insertWidget(trackLayout_->count() - 1, row);
        trackRows_.append(row);
    }
    for (int i = 0; i < tracks.size(); ++i) {
        trackRows_.at(i)->setTrack(i + 1, tracks.at(i), tracks.at(i).id == playingId_);
        if (library_)
            trackRows_.at(i)->setArtwork(library_->thumbnail(tracks.at(i)));
    }

    // The album tile borrows the first track's artwork. There is no album
    // artwork as such in a YouTube Music listing, and an empty square next
    // to a full track list looks like a failure rather than an absence.
    if (!tracks.isEmpty() && library_)
        albumTile_->setArtwork(library_->thumbnail(tracks.first()), tracks.first().id);
    else
        albumTile_->setArtwork(QPixmap(), title);
}

void MusicPage::setAlbumArtwork(const QPixmap &artwork)
{
    albumTile_->setArtwork(artwork, playingId_);
}

void MusicPage::onArtworkReady(const QString &videoId)
{
    if (!library_)
        return;

    const QPixmap artwork = library_->thumbnailIfPresent(videoId);
    if (artwork.isNull())
        return;

    for (int i = 0; i < trackRows_.size(); ++i) {
        if (trackRows_.at(i)->videoId() == videoId)
            trackRows_.at(i)->setArtwork(artwork);
    }
    if (videoId == playingId_)
        nowTile_->setArtwork(artwork, videoId);
    if (!tracks_.isEmpty() && tracks_.first().id == videoId)
        albumTile_->setArtwork(artwork, videoId);
}

void MusicPage::setNowPlaying(const VideoItem &video)
{
    playingId_ = video.id;
    nowTitle_->setText(video.title);
    nowArtist_->setText(video.channelName);
    nowSeek_->setDuration(double(video.duration));

    if (library_)
        nowTile_->setArtwork(library_->thumbnail(video), video.id);

    for (int i = 0; i < trackRows_.size() && i < tracks_.size(); ++i) {
        trackRows_.at(i)->setTrack(i + 1, tracks_.at(i), tracks_.at(i).id == playingId_);
        if (library_)
            trackRows_.at(i)->setArtwork(library_->thumbnail(tracks_.at(i)));
    }
}

void MusicPage::setLyrics(const QList<LyricLine> &lines)
{
    lyrics_->setLines(lines);
    if (lines.isEmpty()) {
        lyricsStatus_->setText(QString::fromUtf8(
            "Sin letra guardada para esta pista.\nSe busca en LRCLIB al reproducir."));
    } else if (lyrics_->synced()) {
        lyricsStatus_->setText(QString::fromUtf8(
            "Fuente: LRCLIB \xC2\xB7 sincronizada\nPulsa una línea para saltar a ese segundo."));
    } else {
        lyricsStatus_->setText(QString::fromUtf8(
            "Letra sin sincronizar: se muestra como texto plano."));
    }
}

void MusicPage::onPositionChanged(double seconds)
{
    nowSeek_->setPosition(seconds);
    nowTime_->setText(SightlinePaint::clockLabel(seconds)
                      + QString::fromLatin1(" / ")
                      + SightlinePaint::clockLabel(playback_->duration()));
    lyrics_->setPosition(seconds);
}
