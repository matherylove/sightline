#include "player_page.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "library.h"
#include "playback.h"
#include "sightline_paint.h"
#include "sightline_style.h"
#include "sightline_window.h"
#include "widgets.h"

// ======================================================== RecommendationRow

RecommendationRow::RecommendationRow(QWidget *parent)
    : QWidget(parent), isNext_(false), hovered_(false)
{
    setFixedHeight(57);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void RecommendationRow::setVideo(const VideoItem &video, bool isNext)
{
    video_ = video;
    isNext_ = isNext;
    setToolTip(video.title);
    update();
}

void RecommendationRow::setArtwork(const QPixmap &artwork)
{
    artwork_ = artwork;
    update();
}

void RecommendationRow::enterEvent(QEvent *event)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void RecommendationRow::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

void RecommendationRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        emit activated(video_.id);
}

void RecommendationRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    if (isNext_) {
        QColor tint = SightlineStyle::teal();
        tint.setAlpha(20);
        painter.fillRect(rect(), tint);
        painter.fillRect(QRect(0, 0, 2, height()), SightlineStyle::teal());
    } else if (hovered_) {
        painter.fillRect(rect(), QColor(255, 255, 255, 8));
    }

    painter.setPen(QColor(SightlineStyle::line().red(), SightlineStyle::line().green(),
                          SightlineStyle::line().blue(), 128));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    const QRect thumbRect(10, 7, 76, 43);
    SightlinePaint::drawArtwork(painter, thumbRect, artwork_, video_.id);
    SightlinePaint::drawFrame(painter, thumbRect, SightlineStyle::line());

    if (video_.duration > 0) {
        painter.setFont(SightlinePaint::monoFont(8));
        const QFontMetrics chipMetrics(painter.font());
        const QString duration = video_.durationLabel();
        const int chipWidth = chipMetrics.width(duration) + 6;
        const QRect chip(thumbRect.right() - chipWidth - 2, thumbRect.bottom() - 12, chipWidth, 11);
        painter.fillRect(chip, QColor(8, 12, 13, 219));
        painter.setPen(SightlineStyle::text());
        painter.drawText(chip, Qt::AlignCenter, duration);
    }

    const int textLeft = thumbRect.right() + 8;
    const int textWidth = width() - textLeft - 10;
    int y = 7;

    if (isNext_) {
        painter.setFont(SightlinePaint::monoFont(8));
        painter.setPen(SightlineStyle::teal());
        painter.drawText(QRect(textLeft, y, textWidth, 11), Qt::AlignTop | Qt::AlignLeft,
                         QString::fromUtf8("SIGUIENTE"));
        y += 12;
    }

    painter.setFont(SightlinePaint::uiFont(10, true));
    painter.setPen(hovered_ ? QColor(255, 255, 255) : SightlineStyle::text());
    const int used = SightlinePaint::drawWrappedText(
        painter, QRect(textLeft, y, textWidth, isNext_ ? 15 : 28), video_.title, isNext_ ? 1 : 2);
    y += used + 1;

    painter.setFont(SightlinePaint::uiFont(9));
    painter.setPen(SightlineStyle::dim());
    QStringList parts;
    if (!video_.channelName.isEmpty())
        parts << video_.channelName;
    const QString views = video_.viewCountLabel();
    if (!views.isEmpty())
        parts << views;
    const QString published = video_.publishedLabel();
    if (!published.isEmpty())
        parts << published;

    const QFontMetrics metrics(painter.font());
    painter.drawText(QRect(textLeft, y, textWidth, 12), Qt::AlignTop | Qt::AlignLeft,
                     metrics.elidedText(parts.join(QString::fromUtf8(" · ")), Qt::ElideRight, textWidth));
}

// ================================================================ CommentRow

CommentRow::CommentRow(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void CommentRow::setComment(const VideoComment &comment)
{
    comment_ = comment;
    setWidthHint(width() > 0 ? width() : 308);
    update();
}

int CommentRow::measuredHeight(int width) const
{
    const int indent = comment_.isReply() ? 26 : 10;
    const int textWidth = qMax(40, width - indent - 10);

    QFont font = SightlinePaint::uiFont(10);
    const QFontMetrics metrics(font);
    const QRect bounds = metrics.boundingRect(QRect(0, 0, textWidth, 4000),
                                              Qt::TextWordWrap, comment_.text);
    // Header, body, footer, plus the padding above and below.
    return 9 + 16 + 4 + bounds.height() + 5 + 12 + 9;
}

void CommentRow::setWidthHint(int width)
{
    setFixedHeight(measuredHeight(width));
}

void CommentRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    const int indent = comment_.isReply() ? 26 : 10;
    if (comment_.isReply())
        painter.fillRect(rect(), QColor(20, 25, 27, 128));

    painter.setPen(QColor(SightlineStyle::line().red(), SightlineStyle::line().green(),
                          SightlineStyle::line().blue(), 128));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    int y = 9;
    const int textWidth = qMax(40, width() - indent - 10);

    // Avatar, name, pinned badge, age.
    painter.fillRect(QRect(indent, y, 16, 16), QColor(0x22, 0x40, 0x3F));
    SightlinePaint::drawFrame(painter, QRect(indent, y, 16, 16), SightlineStyle::line());

    int x = indent + 23;
    painter.setFont(SightlinePaint::uiFont(10, true));
    painter.setPen(comment_.authorIsUploader ? SightlineStyle::teal() : SightlineStyle::text());
    const QFontMetrics nameMetrics(painter.font());
    const QString author = comment_.author.isEmpty()
        ? QString::fromLatin1("@anon") : comment_.author;
    painter.drawText(QRect(x, y, nameMetrics.width(author), 16),
                     Qt::AlignVCenter | Qt::AlignLeft, author);
    x += nameMetrics.width(author) + 7;

    if (comment_.pinned) {
        painter.setFont(SightlinePaint::monoFont(8));
        const QFontMetrics pinMetrics(painter.font());
        const QString pin = QString::fromUtf8("FIJADO");
        const QRect pinRect(x, y + 2, pinMetrics.width(pin) + 6, 12);
        painter.setPen(SightlineStyle::tealDim());
        painter.drawRect(pinRect.adjusted(0, 0, -1, -1));
        painter.setPen(SightlineStyle::teal());
        painter.drawText(pinRect, Qt::AlignCenter, pin);
        x += pinRect.width() + 7;
    }

    if (comment_.published.isValid()) {
        VideoItem probe;
        probe.published = comment_.published;
        painter.setFont(SightlinePaint::monoFont(9));
        painter.setPen(SightlineStyle::faint());
        painter.drawText(QRect(x, y, width() - x - 10, 16),
                         Qt::AlignVCenter | Qt::AlignLeft, probe.publishedLabel());
    }
    y += 20;

    painter.setFont(SightlinePaint::uiFont(10));
    painter.setPen(SightlineStyle::dim());
    const QFontMetrics bodyMetrics(painter.font());
    const QRect bounds = bodyMetrics.boundingRect(QRect(0, 0, textWidth, 4000),
                                                  Qt::TextWordWrap, comment_.text);
    painter.drawText(QRect(indent, y, textWidth, bounds.height()),
                     Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, comment_.text);
    y += bounds.height() + 5;

    painter.setFont(SightlinePaint::monoFont(9));
    const QFontMetrics footMetrics(painter.font());
    int fx = indent;

    painter.setPen(SightlineStyle::teal());
    const QString likes = QString::fromUtf8("\xE2\x96\xB2 ") + QString::number(comment_.likeCount);
    painter.drawText(QRect(fx, y, footMetrics.width(likes), 12),
                     Qt::AlignVCenter | Qt::AlignLeft, likes);
    fx += footMetrics.width(likes) + 12;

    painter.setPen(SightlineStyle::faint());
    const QString reply = QString::fromUtf8("Responder");
    painter.drawText(QRect(fx, y, footMetrics.width(reply), 12),
                     Qt::AlignVCenter | Qt::AlignLeft, reply);
    fx += footMetrics.width(reply) + 12;

    if (comment_.replyCount > 0) {
        painter.setPen(SightlineStyle::teal());
        const QString replies = comment_.replyCount == 1
            ? QString::fromUtf8("1 respuesta")
            : QString::fromUtf8("%1 respuestas").arg(comment_.replyCount);
        painter.drawText(QRect(fx, y, footMetrics.width(replies), 12),
                         Qt::AlignVCenter | Qt::AlignLeft, replies);
    }
}

// ================================================================ PlayerPage

namespace {

QLabel *paneHeading(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1("paneHead"));
    label->setFont(SightlinePaint::capsFont(9));
    label->setFixedHeight(22);
    label->setContentsMargins(10, 0, 10, 0);
    label->setAttribute(Qt::WA_StyledBackground, true);
    return label;
}

QPushButton *transportButton(const QString &text, QWidget *parent, bool accent = false)
{
    QPushButton *button = new QPushButton(text, parent);
    button->setObjectName(accent ? QString::fromLatin1("primaryButton")
                                 : QString::fromLatin1("iconButton"));
    button->setFixedHeight(22);
    button->setMinimumWidth(24);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFont(SightlinePaint::uiFont(10));
    return button;
}

} // namespace

PlayerPage::PlayerPage(Library *library, PlaybackController *playback, QWidget *parent)
    : QWidget(parent),
      library_(library),
      playback_(playback),
      surface_(0), seek_(0), timeLabel_(0), playButton_(0), rateButton_(0),
      channelName_(0), channelMeta_(0), subscribeButton_(0), likeButton_(0),
      paneTabs_(0), panes_(0),
      nextLayout_(0), commentsLayout_(0), formatsLayout_(0),
      commentsHead_(0), expiryLabel_(0), autoplayButton_(0),
      commentsLoaded_(false)
{
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QWidget *stage = new QWidget(this);
    stage->setObjectName(QString::fromLatin1("stage"));
    stage->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);

    surface_ = new VideoSurface(stage);
    stageLayout->addWidget(surface_, 1);
    stageLayout->addWidget(buildTransport());
    stageLayout->addWidget(buildChannelRow());

    root->addWidget(stage, 1);
    root->addWidget(buildSidePane());

    connect(playback_, SIGNAL(positionChanged(double)), this, SLOT(onPositionChanged(double)));
    connect(playback_, SIGNAL(durationChanged(double)), this, SLOT(onDurationChanged(double)));
    connect(playback_, SIGNAL(bufferedChanged(double)), this, SLOT(onBufferedChanged(double)));
    connect(seek_, SIGNAL(seekRequested(double)), playback_, SLOT(seek(double)));
    connect(surface_, SIGNAL(clicked()), playback_, SLOT(togglePause()));
    connect(surface_, SIGNAL(doubleClicked()), this, SIGNAL(fullscreenRequested()));
    connect(surface_, SIGNAL(undoSkipRequested()), playback_, SLOT(undoLastSkip()));

    if (library_)
        connect(library_, SIGNAL(thumbnailReady(QString)), this, SLOT(onThumbnailReady(QString)));
}

QWidget *PlayerPage::buildTransport()
{
    QWidget *transport = new QWidget(this);
    transport->setObjectName(QString::fromLatin1("transport"));
    transport->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *layout = new QVBoxLayout(transport);
    layout->setContentsMargins(10, 8, 10, 9);
    layout->setSpacing(8);

    seek_ = new SeekBar(transport);
    layout->addWidget(seek_);

    QHBoxLayout *row = new QHBoxLayout;
    row->setSpacing(6);

    playButton_ = transportButton(QString::fromUtf8("\xE2\x9D\x9A\xE2\x9D\x9A"), transport, true);
    connect(playButton_, SIGNAL(clicked()), playback_, SLOT(togglePause()));
    row->addWidget(playButton_);

    QPushButton *forward = transportButton(QString::fromUtf8("\xE2\x96\xB8\xE2\x96\xB8"), transport);
    forward->setToolTip(QString::fromUtf8("Avanzar 10 s"));
    row->addWidget(forward);

    timeLabel_ = new QLabel(QString::fromLatin1("00:00 / 00:00"), transport);
    timeLabel_->setFont(SightlinePaint::monoFont(10));
    timeLabel_->setObjectName(QString::fromLatin1("dimLabel"));
    row->addWidget(timeLabel_);

    row->addStretch(1);

    QLabel *volumeLabel = new QLabel(QString::fromUtf8("Vol"), transport);
    volumeLabel->setFont(SightlinePaint::monoFont(10));
    volumeLabel->setObjectName(QString::fromLatin1("faintLabel"));
    row->addWidget(volumeLabel);

    rateButton_ = transportButton(QString::fromLatin1("1x"), transport);
    rateButton_->setToolTip(QString::fromUtf8("Velocidad de reproducción"));
    row->addWidget(rateButton_);

    QPushButton *captions = transportButton(QString::fromLatin1("CC"), transport);
    captions->setToolTip(QString::fromUtf8("Subtítulos"));
    row->addWidget(captions);

    QPushButton *pip = transportButton(QString::fromLatin1("PiP"), transport);
    pip->setToolTip(QString::fromUtf8("Ventana flotante"));
    connect(pip, SIGNAL(clicked()), this, SIGNAL(pipRequested()));
    row->addWidget(pip);

    QPushButton *fullscreen = transportButton(QString::fromUtf8("\xE2\x9B\xB6"), transport);
    fullscreen->setToolTip(QString::fromUtf8("Pantalla completa"));
    connect(fullscreen, SIGNAL(clicked()), this, SIGNAL(fullscreenRequested()));
    row->addWidget(fullscreen);

    layout->addLayout(row);
    return transport;
}

QWidget *PlayerPage::buildChannelRow()
{
    QWidget *actions = new QWidget(this);
    actions->setObjectName(QString::fromLatin1("videoActions"));
    actions->setAttribute(Qt::WA_StyledBackground, true);
    actions->setFixedHeight(43);

    QHBoxLayout *layout = new QHBoxLayout(actions);
    layout->setContentsMargins(10, 9, 10, 9);
    layout->setSpacing(8);

    QLabel *avatar = new QLabel(actions);
    avatar->setFixedSize(24, 24);
    avatar->setStyleSheet(QString::fromLatin1(
        "background: #22403F; border: 1px solid #333E42;"));
    layout->addWidget(avatar);

    QWidget *who = new QWidget(actions);
    QVBoxLayout *whoLayout = new QVBoxLayout(who);
    whoLayout->setContentsMargins(0, 0, 0, 0);
    whoLayout->setSpacing(0);

    channelName_ = new QLabel(who);
    channelName_->setObjectName(QString::fromLatin1("cardTitle"));
    whoLayout->addWidget(channelName_);

    channelMeta_ = new QLabel(who);
    channelMeta_->setObjectName(QString::fromLatin1("cardMeta"));
    whoLayout->addWidget(channelMeta_);

    layout->addWidget(who);

    subscribeButton_ = new QPushButton(QString::fromUtf8("Suscribirse"), actions);
    subscribeButton_->setObjectName(QString::fromLatin1("subscribeButton"));
    subscribeButton_->setCheckable(true);
    subscribeButton_->setFixedHeight(22);
    connect(subscribeButton_, SIGNAL(clicked()), this, SLOT(onSubscribeClicked()));
    layout->addWidget(subscribeButton_);

    QPushButton *bell = new QPushButton(QString::fromUtf8("\xE2\x97\x94"), actions);
    bell->setObjectName(QString::fromLatin1("iconButton"));
    bell->setFixedSize(24, 22);
    bell->setToolTip(QString::fromUtf8("Avisarme de vídeos nuevos"));
    layout->addWidget(bell);

    layout->addStretch(1);

    likeButton_ = new QPushButton(QString::fromUtf8("Me gusta"), actions);
    likeButton_->setFixedHeight(22);
    layout->addWidget(likeButton_);

    QPushButton *save = new QPushButton(QString::fromUtf8("Guardar en\xE2\x80\xA6"), actions);
    save->setFixedHeight(22);
    connect(save, SIGNAL(clicked()), this, SIGNAL(saveToPlaylistRequested()));
    layout->addWidget(save);

    QPushButton *download = new QPushButton(QString::fromUtf8("Descargar\xE2\x80\xA6"), actions);
    download->setObjectName(QString::fromLatin1("primaryButton"));
    download->setFixedHeight(22);
    connect(download, SIGNAL(clicked()), this, SIGNAL(downloadRequested()));
    layout->addWidget(download);

    return actions;
}

QWidget *PlayerPage::buildSidePane()
{
    QWidget *pane = new QWidget(this);
    pane->setObjectName(QString::fromLatin1("sidePane"));
    pane->setAttribute(Qt::WA_StyledBackground, true);
    pane->setFixedWidth(308);

    QVBoxLayout *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget *tabRow = new QWidget(pane);
    tabRow->setFixedHeight(24);
    QHBoxLayout *tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);

    paneTabs_ = new QButtonGroup(this);
    paneTabs_->setExclusive(true);

    const char *titles[] = { "A continuaci\xC3\xB3n", "Comentarios", "Formatos" };
    for (int i = 0; i < 3; ++i) {
        QPushButton *tab = new QPushButton(QString::fromUtf8(titles[i]), tabRow);
        tab->setObjectName(QString::fromLatin1("paneTab"));
        tab->setCheckable(true);
        tab->setChecked(i == 0);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setFixedHeight(24);
        paneTabs_->addButton(tab, i);
        tabLayout->addWidget(tab, 1);
    }
    connect(paneTabs_, SIGNAL(buttonClicked(int)), this, SLOT(onPaneChanged(int)));
    layout->addWidget(tabRow);

    panes_ = new QStackedWidget(pane);

    // --- next up + recommendations ---------------------------------------
    QScrollArea *nextScroll = new QScrollArea(panes_);
    nextScroll->setWidgetResizable(true);
    nextScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nextScroll->setFrameShape(QFrame::NoFrame);
    QWidget *nextInner = new QWidget(nextScroll);
    nextLayout_ = new QVBoxLayout(nextInner);
    nextLayout_->setContentsMargins(0, 0, 0, 0);
    nextLayout_->setSpacing(0);
    nextLayout_->addStretch(1);
    nextScroll->setWidget(nextInner);
    panes_->addWidget(nextScroll);

    // --- comments ---------------------------------------------------------
    QScrollArea *commentsScroll = new QScrollArea(panes_);
    commentsScroll->setWidgetResizable(true);
    commentsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    commentsScroll->setFrameShape(QFrame::NoFrame);
    QWidget *commentsInner = new QWidget(commentsScroll);
    commentsLayout_ = new QVBoxLayout(commentsInner);
    commentsLayout_->setContentsMargins(0, 0, 0, 0);
    commentsLayout_->setSpacing(0);

    commentsHead_ = paneHeading(QString::fromUtf8("Sin cargar"), commentsInner);
    commentsLayout_->addWidget(commentsHead_);
    commentsLayout_->addStretch(1);
    commentsScroll->setWidget(commentsInner);
    panes_->addWidget(commentsScroll);

    // --- formats ----------------------------------------------------------
    QScrollArea *formatsScroll = new QScrollArea(panes_);
    formatsScroll->setWidgetResizable(true);
    formatsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    formatsScroll->setFrameShape(QFrame::NoFrame);
    QWidget *formatsInner = new QWidget(formatsScroll);
    formatsLayout_ = new QVBoxLayout(formatsInner);
    formatsLayout_->setContentsMargins(0, 0, 0, 0);
    formatsLayout_->setSpacing(0);
    formatsLayout_->addStretch(1);
    formatsScroll->setWidget(formatsInner);
    panes_->addWidget(formatsScroll);

    layout->addWidget(panes_, 1);

    expiryLabel_ = new QLabel(pane);
    expiryLabel_->setFont(SightlinePaint::monoFont(9));
    expiryLabel_->setObjectName(QString::fromLatin1("faintLabel"));
    expiryLabel_->setWordWrap(true);
    expiryLabel_->setContentsMargins(10, 8, 10, 8);
    expiryLabel_->setStyleSheet(QString::fromLatin1("border-top: 1px solid #333E42;"));
    layout->addWidget(expiryLabel_);

    return pane;
}

void PlayerPage::onPaneChanged(int pane)
{
    panes_->setCurrentIndex(pane);

    // Comments cost a separate, slow call to yt-dlp, so they are fetched
    // when the tab is opened rather than with the video.
    if (pane == CommentsPane && !commentsLoaded_) {
        commentsLoaded_ = true;
        setCommentsLoading(true);
        emit commentsRequested();
    }
}

void PlayerPage::setVideo(const VideoItem &video)
{
    video_ = video;
    commentsLoaded_ = false;

    channelName_->setText(video.channelName);
    QStringList meta;
    const QString views = video.viewCountLabel();
    if (!views.isEmpty())
        meta << views + QString::fromUtf8(" visualizaciones");
    const QString published = video.publishedLabel();
    if (!published.isEmpty())
        meta << published;
    channelMeta_->setText(meta.join(QString::fromUtf8(" · ")));

    if (video.likeCount > 0) {
        VideoItem probe;
        probe.viewCount = video.likeCount;
        likeButton_->setText(QString::fromUtf8("Me gusta  ") + probe.viewCountLabel());
    } else {
        likeButton_->setText(QString::fromUtf8("Me gusta"));
    }

    // Reset the comments pane so the previous video's thread is not left on
    // screen while the new one loads.
    while (commentsLayout_->count() > 2) {
        QLayoutItem *item = commentsLayout_->takeAt(1);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    commentsHead_->setText(QString::fromUtf8("Abre esta pestaña para cargarlos"));

    rebuildFormats();
    refreshOverlay();
    refreshTimeLabel();
    paneTabs_->button(NextPane)->setChecked(true);
    panes_->setCurrentIndex(NextPane);
}

void PlayerPage::setSubscribed(bool subscribed)
{
    subscribeButton_->setChecked(subscribed);
    subscribeButton_->setText(subscribed ? QString::fromUtf8("Suscrito \xE2\x9C\x93")
                                         : QString::fromUtf8("Suscribirse"));
}

void PlayerPage::onSubscribeClicked()
{
    const bool subscribed = subscribeButton_->isChecked();
    setSubscribed(subscribed);
    emit subscribeToggled(subscribed);
}

void PlayerPage::setUrlExpiry(const QString &text)
{
    expiryLabel_->setText(text);
}

void PlayerPage::setRecommendations(const QList<VideoItem> &videos,
                                    const QList<VideoItem> &channelVideos)
{
    while (nextLayout_->count() > 1) {
        QLayoutItem *item = nextLayout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    recommendationRows_.clear();
    recommendations_.clear();

    QWidget *inner = nextLayout_->parentWidget();
    int insertAt = 0;

    if (!channelVideos.isEmpty()) {
        QLabel *head = paneHeading(QString::fromUtf8("De este canal"), inner);
        nextLayout_->insertWidget(insertAt++, head);

        for (int i = 0; i < channelVideos.size() && i < 3; ++i) {
            RecommendationRow *row = new RecommendationRow(inner);
            row->setVideo(channelVideos.at(i), i == 0);
            if (library_)
                row->setArtwork(library_->thumbnail(channelVideos.at(i)));
            connect(row, SIGNAL(activated(QString)), this, SIGNAL(playRequested(QString)));
            nextLayout_->insertWidget(insertAt++, row);
            recommendationRows_.append(row);
            recommendations_.append(channelVideos.at(i));
        }
    }

    if (!videos.isEmpty()) {
        QLabel *head = paneHeading(QString::fromUtf8("Recomendados"), inner);
        nextLayout_->insertWidget(insertAt++, head);

        for (int i = 0; i < videos.size(); ++i) {
            RecommendationRow *row = new RecommendationRow(inner);
            row->setVideo(videos.at(i), channelVideos.isEmpty() && i == 0);
            if (library_)
                row->setArtwork(library_->thumbnail(videos.at(i)));
            connect(row, SIGNAL(activated(QString)), this, SIGNAL(playRequested(QString)));
            nextLayout_->insertWidget(insertAt++, row);
            recommendationRows_.append(row);
            recommendations_.append(videos.at(i));
        }
    }
}

QString PlayerPage::nextVideoId() const
{
    return recommendations_.isEmpty() ? QString() : recommendations_.first().id;
}

bool PlayerPage::autoplayEnabled() const
{
    return true;
}

void PlayerPage::onThumbnailReady(const QString &videoId)
{
    if (!library_)
        return;
    for (int i = 0; i < recommendationRows_.size(); ++i) {
        if (recommendationRows_.at(i)->videoId() == videoId)
            recommendationRows_.at(i)->setArtwork(library_->thumbnailIfPresent(videoId));
    }
}

void PlayerPage::setCommentsLoading(bool loading)
{
    if (loading)
        commentsHead_->setText(QString::fromUtf8("Cargando comentarios\xE2\x80\xA6"));
}

void PlayerPage::setComments(const QList<VideoComment> &comments)
{
    while (commentsLayout_->count() > 2) {
        QLayoutItem *item = commentsLayout_->takeAt(1);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (comments.isEmpty()) {
        commentsHead_->setText(QString::fromUtf8("Sin comentarios o desactivados"));
        return;
    }

    commentsHead_->setText(QString::fromUtf8("%1 comentarios").arg(comments.size()));

    QWidget *inner = commentsLayout_->parentWidget();
    int insertAt = 1;
    for (int i = 0; i < comments.size(); ++i) {
        CommentRow *row = new CommentRow(inner);
        row->setComment(comments.at(i));
        row->setWidthHint(288);
        commentsLayout_->insertWidget(insertAt++, row);
    }

    QLabel *note = new QLabel(QString::fromUtf8(
        "Los comentarios se piden aparte y tardan unos segundos."), inner);
    note->setFont(SightlinePaint::monoFont(9));
    note->setObjectName(QString::fromLatin1("faintLabel"));
    note->setWordWrap(true);
    note->setContentsMargins(10, 9, 10, 9);
    note->setStyleSheet(QString::fromLatin1("border-top: 1px solid #333E42;"));
    commentsLayout_->insertWidget(insertAt, note);
}

void PlayerPage::setSegments(const QList<SponsorSegment> &segments)
{
    seek_->setSegments(segments);
}

// A format row: itag, quality, bitrate. Formats the CPU policy excludes stay
// visible in grey rather than being hidden, because hiding them would look
// like they do not exist.
class FormatRow : public QWidget
{
public:
    FormatRow(const MediaFormat &format, bool selected, bool excluded, QWidget *parent)
        : QWidget(parent), format_(format), selected_(selected), excluded_(excluded)
    {
        setFixedHeight(24);
        setCursor(excluded ? Qt::ArrowCursor : Qt::PointingHandCursor);
        setProperty("itag", format.itag);
    }

protected:
    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        if (selected_) {
            QColor tint = SightlineStyle::teal();
            tint.setAlpha(26);
            painter.fillRect(rect(), tint);
            painter.fillRect(QRect(0, 0, 2, height()), SightlineStyle::teal());
        }
        painter.setPen(QColor(SightlineStyle::line().red(), SightlineStyle::line().green(),
                              SightlineStyle::line().blue(), 128));
        painter.drawLine(0, height() - 1, width(), height() - 1);

        const QColor strong = excluded_ ? SightlineStyle::faint()
                                        : (selected_ ? SightlineStyle::text() : SightlineStyle::text());
        const QColor weak = excluded_ ? SightlineStyle::faint() : SightlineStyle::dim();

        painter.setFont(SightlinePaint::monoFont(10));
        painter.setPen(strong);
        painter.drawText(QRect(10, 0, 34, height()), Qt::AlignVCenter | Qt::AlignLeft, format_.itag);

        QString label = format_.qualityLabel();
        if (format_.hasVideo()) {
            if (format_.isAvc())
                label += QString::fromLatin1(" avc1");
            else if (format_.videoCodec.startsWith(QLatin1String("vp9")))
                label += QString::fromLatin1(" vp9");
            else if (format_.videoCodec.startsWith(QLatin1String("av01")))
                label += QString::fromLatin1(" av01");
            if (!format_.hasAudio())
                label += QString();
            else
                label += QString::fromUtf8(" muxeado");
        }
        painter.setPen(weak);
        painter.drawText(QRect(50, 0, width() - 120, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, label);

        painter.setPen(SightlineStyle::faint());
        const QString right = excluded_ ? QString::fromUtf8("omitido") : format_.bitrateLabel();
        painter.drawText(QRect(width() - 74, 0, 64, height()),
                         Qt::AlignVCenter | Qt::AlignRight, right);
    }

    void mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton && !excluded_ && parentWidget())
            QMetaObject::invokeMethod(window(), "formatRowActivated",
                                      Q_ARG(QString, format_.itag));
    }

private:
    MediaFormat format_;
    bool selected_;
    bool excluded_;
};

void PlayerPage::rebuildFormats()
{
    while (formatsLayout_->count() > 1) {
        QLayoutItem *item = formatsLayout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    QWidget *inner = formatsLayout_->parentWidget();
    int insertAt = 0;

    formatsLayout_->insertWidget(insertAt++,
        paneHeading(QString::fromUtf8("Formatos disponibles"), inner));

    // Video first, ordered tallest to shortest, then the audio-only tracks.
    QList<MediaFormat> videos;
    QList<MediaFormat> audios;
    for (int i = 0; i < video_.formats.size(); ++i) {
        const MediaFormat &format = video_.formats.at(i);
        if (format.hasVideo())
            videos.append(format);
        else if (format.hasAudio())
            audios.append(format);
    }
    for (int a = 1; a < videos.size(); ++a) {
        const MediaFormat key = videos.at(a);
        int b = a - 1;
        while (b >= 0 && videos.at(b).height < key.height) {
            videos[b + 1] = videos.at(b);
            --b;
        }
        videos[b + 1] = key;
    }

    if (currentItag_.isEmpty() && !videos.isEmpty())
        currentItag_ = videos.first().itag;

    for (int i = 0; i < videos.size(); ++i) {
        const MediaFormat &format = videos.at(i);
        FormatRow *row = new FormatRow(format, format.itag == currentItag_,
                                       !format.isAvc(), inner);
        formatsLayout_->insertWidget(insertAt++, row);
    }

    if (!audios.isEmpty()) {
        formatsLayout_->insertWidget(insertAt++,
            paneHeading(QString::fromUtf8("Pista de audio"), inner));
        for (int i = 0; i < audios.size(); ++i) {
            FormatRow *row = new FormatRow(audios.at(i), i == 0, false, inner);
            formatsLayout_->insertWidget(insertAt++, row);
        }
    }

    if (videos.isEmpty() && audios.isEmpty()) {
        QLabel *empty = new QLabel(QString::fromUtf8(
            "Todavía no se han resuelto los formatos."), inner);
        empty->setObjectName(QString::fromLatin1("faintLabel"));
        empty->setContentsMargins(10, 10, 10, 10);
        empty->setWordWrap(true);
        formatsLayout_->insertWidget(insertAt, empty);
    }
}

void PlayerPage::onFormatRowClicked()
{
}

void PlayerPage::refreshOverlay()
{
    QStringList lines;
    lines << QString::fromUtf8(">Superficie D3D9 \xC2\xB7 YV12 \xE2\x86\x92 shader PS 2.0");

    if (!playback_->videoCodecLabel().isEmpty()) {
        QString line = playback_->videoCodecLabel();
        if (!playback_->resolutionLabel().isEmpty())
            line += QString::fromUtf8(" \xC2\xB7 ") + playback_->resolutionLabel();
        lines << line;
    }
    lines << QString::fromUtf8("Decodificaci\xC3\xB3n software (XP no expone DXVA2)");
    lines << QString::fromUtf8("B\xC3\xBA""fer %1 s \xC2\xB7 reloj maestro: audio")
                 .arg(QString::number(qMax(0.0, playback_->buffered() - playback_->position()), 'f', 1));
    surface_->setOverlayLines(lines);
}

void PlayerPage::refreshTimeLabel()
{
    timeLabel_->setText(SightlinePaint::clockLabel(playback_->position())
                        + QString::fromLatin1(" / ")
                        + SightlinePaint::clockLabel(playback_->duration()));
}

void PlayerPage::onPositionChanged(double seconds)
{
    seek_->setPosition(seconds);
    refreshTimeLabel();
}

void PlayerPage::onDurationChanged(double seconds)
{
    seek_->setDuration(seconds);
    refreshTimeLabel();
}

void PlayerPage::onBufferedChanged(double seconds)
{
    seek_->setBuffered(seconds);
    refreshOverlay();
}

void PlayerPage::onStateChanged(int state)
{
    playButton_->setText(state == PlaybackController::Playing
        ? QString::fromUtf8("\xE2\x9D\x9A\xE2\x9D\x9A")
        : QString::fromUtf8("\xE2\x96\xB8"));
}
