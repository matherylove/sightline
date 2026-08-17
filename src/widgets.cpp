#include "widgets.h"

#include <QContextMenuEvent>
#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygon>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#include "library.h"
#include "sightline_paint.h"
#include "sightline_style.h"

// ========================================================= SightlineStatusBar

SightlineStatusBar::SightlineStatusBar(QWidget *parent)
    : QWidget(parent), level_(Ready), blinkOn_(true)
{
    setFixedHeight(24);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    stateText_ = QString::fromUtf8("Listo");

    for (int i = 0; i < 6; ++i)
        cells_.append(Cell());
    cells_[4].grow = true;

    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(onBlink()));
    timer->start(1200);
}

void SightlineStatusBar::onBlink()
{
    blinkOn_ = !blinkOn_;
    update(0, 0, 24, height());
}

QSize SightlineStatusBar::sizeHint() const
{
    return QSize(400, 24);
}

void SightlineStatusBar::setState(Level level, const QString &text)
{
    level_ = level;
    stateText_ = text;
    update();
}

void SightlineStatusBar::setCell(int index, const QString &text, Level level)
{
    if (index < 0 || index >= cells_.size())
        return;
    cells_[index].text = text;
    cells_[index].level = level;
    update();
}

void SightlineStatusBar::setGrowCell(const QString &text)
{
    cells_[4].text = text;
    update();
}

void SightlineStatusBar::setTrailing(const QString &text)
{
    cells_[5].text = text;
    update();
}

void SightlineStatusBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), SightlineStyle::raise());
    painter.setPen(SightlineStyle::line());
    painter.drawLine(0, 0, width(), 0);

    painter.setFont(SightlinePaint::monoFont(10));
    const QFontMetrics metrics(painter.font());

    // The state cell leads, with the live dot. Amber for anything degraded,
    // teal when the whole chain is up.
    const QColor stateInk = (level_ == Degraded) ? SightlineStyle::amber()
                                                 : SightlineStyle::text();
    const QColor dotInk = (level_ == Degraded) ? SightlineStyle::amber()
                                               : SightlineStyle::teal();

    int x = 0;
    const int dotSize = 6;
    const int paddingH = 10;

    int cellWidth = paddingH + dotSize + 6 + metrics.width(stateText_) + paddingH;
    if (blinkOn_ || level_ == Ready)
        painter.fillRect(QRect(x + paddingH, (height() - dotSize) / 2, dotSize, dotSize), dotInk);
    painter.setPen(stateInk);
    painter.drawText(QRect(x + paddingH + dotSize + 6, 0, cellWidth, height()),
                     Qt::AlignVCenter | Qt::AlignLeft, stateText_);
    x += cellWidth;
    painter.setPen(SightlineStyle::line());
    painter.drawLine(x, 2, x, height() - 3);

    // Fixed cells are measured first so whatever is left goes to the grow
    // cell, which is where the long free-text line lives.
    int fixedWidth = 0;
    for (int i = 0; i < cells_.size(); ++i) {
        if (cells_.at(i).grow || cells_.at(i).text.isEmpty())
            continue;
        fixedWidth += metrics.width(cells_.at(i).text) + paddingH * 2;
    }
    const int growWidth = qMax(0, width() - x - fixedWidth);

    for (int i = 0; i < cells_.size(); ++i) {
        const Cell &cell = cells_.at(i);
        if (cell.text.isEmpty() && !cell.grow)
            continue;

        const int thisWidth = cell.grow ? growWidth
                                        : metrics.width(cell.text) + paddingH * 2;
        if (thisWidth <= 0)
            continue;

        QColor ink = SightlineStyle::dim();
        if (cell.level == Degraded)
            ink = SightlineStyle::amber();
        else if (cell.level == Working)
            ink = SightlineStyle::teal();

        painter.setPen(ink);
        const QRect textRect(x + paddingH, 0, thisWidth - paddingH * 2, height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                         metrics.elidedText(cell.text, Qt::ElideRight, textRect.width()));
        x += thisWidth;

        if (i < cells_.size() - 1 && x < width()) {
            painter.setPen(SightlineStyle::line());
            painter.drawLine(x, 2, x, height() - 3);
        }
    }
}

// ============================================================== SidebarItem

SidebarItem::SidebarItem(const QString &glyph, const QString &label, const QString &key, QWidget *parent)
    : QWidget(parent), glyph_(glyph), label_(label), key_(key),
      count_(-1), selected_(false), hovered_(false), filled_(false)
{
    setFixedHeight(22);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void SidebarItem::setCount(int count)
{
    count_ = count;
    update();
}

void SidebarItem::setSelected(bool selected)
{
    if (selected_ == selected)
        return;
    selected_ = selected;
    update();
}

void SidebarItem::setFilled(bool filled)
{
    filled_ = filled;
    update();
}

void SidebarItem::enterEvent(QEvent *event)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void SidebarItem::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

void SidebarItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        emit activated(key_);
}

void SidebarItem::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    if (selected_) {
        QColor tint = SightlineStyle::teal();
        tint.setAlpha(26);
        painter.fillRect(rect(), tint);
        painter.fillRect(QRect(0, 0, 2, height()), SightlineStyle::teal());
    } else if (hovered_) {
        painter.fillRect(rect(), QColor(255, 255, 255, 8));
    }

    const QColor ink = selected_ ? SightlineStyle::text() : SightlineStyle::dim();
    const QColor glyphInk = selected_ ? SightlineStyle::teal() : SightlineStyle::faint();

    painter.setFont(SightlinePaint::uiFont(10));
    painter.setPen(glyphInk);
    painter.drawText(QRect(8, 0, 12, height()), Qt::AlignCenter,
                     filled_ ? QString::fromUtf8("\xE2\x97\x8F") : glyph_);

    int right = width() - 10;
    if (count_ >= 0) {
        painter.setFont(SightlinePaint::monoFont(10));
        painter.setPen(SightlineStyle::faint());
        const QString countText = QString::number(count_);
        const QFontMetrics metrics(painter.font());
        const int countWidth = metrics.width(countText);
        painter.drawText(QRect(width() - 10 - countWidth, 0, countWidth, height()),
                         Qt::AlignVCenter | Qt::AlignRight, countText);
        right = width() - 14 - countWidth;
    }

    painter.setFont(SightlinePaint::uiFont(11));
    painter.setPen(ink);
    const QRect textRect(28, 0, qMax(0, right - 28), height());
    const QFontMetrics metrics(painter.font());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     metrics.elidedText(label_, Qt::ElideRight, textRect.width()));
}

// ============================================================= SidebarGroup

SidebarGroup::SidebarGroup(const QString &title, QWidget *parent)
    : QWidget(parent), title_(title)
{
    setFixedHeight(28);
}

void SidebarGroup::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setFont(SightlinePaint::capsFont(9));
    painter.setPen(SightlineStyle::faint());
    painter.drawText(QRect(10, 12, width() - 20, 16), Qt::AlignBottom | Qt::AlignLeft, title_);
}

// ================================================================== Sidebar

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent), layout_(0)
{
    setObjectName(QString::fromLatin1("sidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(186);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    layout_->addStretch(1);
}

void Sidebar::beginRebuild()
{
    // Clearing by walking the layout rather than deleting children keeps the
    // trailing stretch, which would otherwise have to be re-added by hand.
    while (layout_->count() > 1) {
        QLayoutItem *item = layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    items_.clear();
}

void Sidebar::addGroup(const QString &title)
{
    layout_->insertWidget(layout_->count() - 1, new SidebarGroup(title, this));
}

SidebarItem *Sidebar::addItem(const QString &glyph, const QString &label, const QString &key,
                              int count, bool filled)
{
    SidebarItem *item = new SidebarItem(glyph, label, key, this);
    if (count >= 0)
        item->setCount(count);
    item->setFilled(filled);
    item->setSelected(key == selected_);
    connect(item, SIGNAL(activated(QString)), this, SLOT(onItemActivated(QString)));
    layout_->insertWidget(layout_->count() - 1, item);
    items_.append(item);
    return item;
}

void Sidebar::endRebuild()
{
    update();
}

void Sidebar::onItemActivated(const QString &key)
{
    setSelectedKey(key);
    emit itemActivated(key);
}

void Sidebar::setSelectedKey(const QString &key)
{
    selected_ = key;
    for (int i = 0; i < items_.size(); ++i)
        items_.at(i)->setSelected(items_.at(i)->key() == key);
}

// ================================================================ VideoCard

VideoCard::VideoCard(QWidget *parent)
    : QWidget(parent), hovered_(false)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(140);
}

void VideoCard::setVideo(const VideoItem &video)
{
    video_ = video;
    setToolTip(video.title);
    update();
}

void VideoCard::setArtwork(const QPixmap &artwork)
{
    artwork_ = artwork;
    update();
}

void VideoCard::enterEvent(QEvent *event)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void VideoCard::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

void VideoCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        emit activated(video_.id);
}

void VideoCard::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextRequested(video_.id, event->globalPos());
}

void VideoCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    const int thumbHeight = width() * 9 / 16;
    const QRect thumbRect(0, 0, width(), thumbHeight);

    SightlinePaint::drawArtwork(painter, thumbRect, artwork_, video_.id);
    SightlinePaint::drawFrame(painter, thumbRect,
                              hovered_ ? SightlineStyle::teal() : SightlineStyle::line());

    if (video_.duration > 0)
        SightlinePaint::drawDurationChip(painter, thumbRect, video_.durationLabel());

    if (video_.resumePosition > 0 && video_.duration > 0) {
        SightlinePaint::drawResumeBar(painter, thumbRect,
                                      double(video_.resumePosition) / double(video_.duration));
    }

    painter.setFont(SightlinePaint::uiFont(11, true));
    painter.setPen(hovered_ ? QColor(255, 255, 255) : SightlineStyle::text());
    const QRect titleRect(0, thumbHeight + 6, width(), 30);
    SightlinePaint::drawWrappedText(painter, titleRect, video_.title, 2);

    painter.setFont(SightlinePaint::uiFont(10));
    painter.setPen(SightlineStyle::dim());

    QStringList parts;
    if (!video_.channelName.isEmpty())
        parts << video_.channelName;
    const QString published = video_.publishedLabel();
    if (!published.isEmpty())
        parts << published;
    const QString views = video_.viewCountLabel();
    if (!views.isEmpty())
        parts << views;

    const QRect metaRect(0, thumbHeight + 38, width(), 14);
    const QFontMetrics metrics(painter.font());
    painter.drawText(metaRect, Qt::AlignTop | Qt::AlignLeft,
                     metrics.elidedText(parts.join(QString::fromUtf8(" · ")),
                                        Qt::ElideRight, width()));
}

// ================================================================ VideoGrid

VideoGrid::VideoGrid(Library *library, QWidget *parent)
    : QWidget(parent), library_(library)
{
    emptyText_ = QString::fromUtf8("No hay nada que mostrar todavía.");
    if (library_)
        connect(library_, SIGNAL(thumbnailReady(QString)), this, SLOT(onThumbnailReady(QString)));
}

void VideoGrid::setEmptyText(const QString &text)
{
    emptyText_ = text;
    update();
}

void VideoGrid::clear()
{
    for (int i = 0; i < cards_.size(); ++i)
        cards_.at(i)->deleteLater();
    cards_.clear();
    videos_.clear();
    update();
}

void VideoGrid::setVideos(const QList<VideoItem> &videos)
{
    // Reuse the cards already built. Rebuilding forty widgets on every view
    // change is visible as a flash on the hardware this targets.
    while (cards_.size() > videos.size()) {
        cards_.last()->deleteLater();
        cards_.removeLast();
    }
    while (cards_.size() < videos.size()) {
        VideoCard *card = new VideoCard(this);
        connect(card, SIGNAL(activated(QString)), this, SIGNAL(videoActivated(QString)));
        connect(card, SIGNAL(contextRequested(QString, QPoint)),
                this, SIGNAL(videoContextRequested(QString, QPoint)));
        card->show();
        cards_.append(card);
    }

    videos_ = videos;
    for (int i = 0; i < videos.size(); ++i) {
        cards_.at(i)->setVideo(videos.at(i));
        if (library_)
            cards_.at(i)->setArtwork(library_->thumbnail(videos.at(i)));
    }
    relayout();
    update();
}

void VideoGrid::onThumbnailReady(const QString &videoId)
{
    if (!library_)
        return;
    for (int i = 0; i < cards_.size(); ++i) {
        if (cards_.at(i)->video().id == videoId) {
            cards_.at(i)->setArtwork(library_->thumbnailIfPresent(videoId));
            return;
        }
    }
}

void VideoGrid::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void VideoGrid::relayout()
{
    const int margin = 10;
    const int gap = 10;
    const int available = width() - margin * 2;
    if (available <= 0)
        return;

    // Four columns at full width, dropping to three and then two as the
    // window narrows, which is what the mockup specifies.
    int columns = 4;
    if (available < 520)
        columns = 2;
    else if (available < 720)
        columns = 3;

    const int cardWidth = (available - gap * (columns - 1)) / columns;
    const int cardHeight = cardWidth * 9 / 16 + 54;

    for (int i = 0; i < cards_.size(); ++i) {
        const int column = i % columns;
        const int row = i / columns;
        cards_.at(i)->setGeometry(margin + column * (cardWidth + gap),
                                  margin + row * (cardHeight + gap),
                                  cardWidth, cardHeight);
    }

    const int rows = (cards_.size() + columns - 1) / columns;
    setMinimumHeight(margin * 2 + rows * (cardHeight + gap));
}

void VideoGrid::paintEvent(QPaintEvent *)
{
    if (!cards_.isEmpty())
        return;

    QPainter painter(this);
    painter.setFont(SightlinePaint::uiFont(11));
    painter.setPen(SightlineStyle::dim());
    painter.drawText(rect().adjusted(20, 40, -20, 0), Qt::AlignTop | Qt::AlignHCenter, emptyText_);
}

// ================================================================== SeekBar

SeekBar::SeekBar(QWidget *parent)
    : QWidget(parent), duration_(0.0), position_(0.0), buffered_(0.0),
      dragging_(false), compact_(false)
{
    setFixedHeight(12);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

QSize SeekBar::sizeHint() const
{
    return QSize(200, compact_ ? 9 : 12);
}

void SeekBar::setCompact(bool compact)
{
    compact_ = compact;
    setFixedHeight(compact ? 9 : 12);
    update();
}

void SeekBar::setDuration(double seconds)
{
    duration_ = qMax(0.0, seconds);
    update();
}

void SeekBar::setPosition(double seconds)
{
    position_ = qBound(0.0, seconds, duration_ > 0 ? duration_ : seconds);
    update();
}

void SeekBar::setBuffered(double seconds)
{
    buffered_ = qMax(0.0, seconds);
    update();
}

void SeekBar::setSegments(const QList<SponsorSegment> &segments)
{
    segments_ = segments;
    update();
}

double SeekBar::positionAt(int x) const
{
    if (duration_ <= 0.0 || width() <= 0)
        return 0.0;
    return qBound(0.0, double(x) / double(width()) * duration_, duration_);
}

void SeekBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    dragging_ = true;
    setPosition(positionAt(event->pos().x()));
    emit seekRequested(position_);
}

void SeekBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_)
        return;
    setPosition(positionAt(event->pos().x()));
    emit seekRequested(position_);
}

void SeekBar::mouseReleaseEvent(QMouseEvent *)
{
    dragging_ = false;
}

void SeekBar::leaveEvent(QEvent *)
{
    dragging_ = false;
}

void SeekBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const int barHeight = compact_ ? 3 : 6;
    const int top = (height() - barHeight) / 2;
    painter.fillRect(QRect(0, top, width(), barHeight), SightlineStyle::sink());

    if (duration_ <= 0.0)
        return;

    const int bufferedWidth = int(width() * qMin(1.0, buffered_ / duration_));
    painter.fillRect(QRect(0, top, bufferedWidth, barHeight), SightlineStyle::line());

    const int playedWidth = int(width() * qMin(1.0, position_ / duration_));
    painter.fillRect(QRect(0, top, playedWidth, barHeight), SightlineStyle::teal());

    // Segments sit on top of the fill, not beside it: this is the progress
    // bar itself carrying the marks, exactly as the mockup specifies.
    for (int i = 0; i < segments_.size(); ++i) {
        const SponsorSegment &segment = segments_.at(i);
        QColor colour;
        switch (segment.category) {
        case SponsorSegment::Sponsor:       colour = SightlineStyle::sbSponsor(); break;
        case SponsorSegment::Intro:         colour = SightlineStyle::sbIntro(); break;
        case SponsorSegment::SelfPromo:     colour = SightlineStyle::sbPromo(); break;
        case SponsorSegment::Interaction:   colour = SightlineStyle::sbInter(); break;
        case SponsorSegment::MusicOffTopic: colour = SightlineStyle::sbMusic(); break;
        default: continue;
        }

        const int left = int(width() * qBound(0.0, segment.start / duration_, 1.0));
        const int right = int(width() * qBound(0.0, segment.end / duration_, 1.0));
        painter.fillRect(QRect(left, top, qMax(2, right - left), barHeight), colour);
    }

    const int knobHeight = compact_ ? 9 : 12;
    painter.fillRect(QRect(qMax(0, playedWidth - 1), (height() - knobHeight) / 2, 3, knobHeight),
                     QColor(255, 255, 255));
}

// ============================================================== VideoSurface

VideoSurface::VideoSurface(QWidget *parent)
    : QWidget(parent), overlayVisible_(true), toastVisible_(false)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // A native handle, because the Direct3D 9 swap chain will be created
    // against this window. WA_PaintOnScreen stays off until a device is
    // actually attached, so Qt keeps painting the canvas until then.
    setAttribute(Qt::WA_NativeWindow, true);
    setMinimumSize(160, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

WId VideoSurface::surfaceHandle() const
{
    return const_cast<VideoSurface *>(this)->winId();
}

void VideoSurface::presentFrame(const QImage &frame)
{
    frame_ = frame;
    update();
}

void VideoSurface::clearFrame()
{
    frame_ = QImage();
    update();
}

void VideoSurface::setOverlayLines(const QStringList &lines)
{
    overlayLines_ = lines;
    update();
}

void VideoSurface::setOverlayVisible(bool visible)
{
    overlayVisible_ = visible;
    update();
}

void VideoSurface::setSkipToast(const QString &category, const QString &detail, const QColor &colour)
{
    toastCategory_ = category;
    toastDetail_ = detail;
    toastColour_ = colour;
    toastVisible_ = true;
    update();

    // Five seconds to undo. Long enough to react to, short enough that it is
    // gone before the next segment arrives.
    QTimer::singleShot(5000, this, SLOT(onToastTimeout()));
}

void VideoSurface::clearSkipToast()
{
    toastVisible_ = false;
    update();
}

void VideoSurface::onToastTimeout()
{
    clearSkipToast();
}

QRect VideoSurface::undoRect() const
{
    return undoRect_;
}

void VideoSurface::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (toastVisible_ && undoRect_.contains(event->pos())) {
        clearSkipToast();
        emit undoSkipRequested();
        return;
    }
    emit clicked();
}

void VideoSurface::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit doubleClicked();
}

void VideoSurface::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    if (!frame_.isNull()) {
        // Letterboxed, never stretched: the aspect ratio of the decoded
        // frame is authoritative and the bars are part of the picture.
        const QSize scaled = frame_.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target((width() - scaled.width()) / 2,
                           (height() - scaled.height()) / 2,
                           scaled.width(), scaled.height());
        painter.fillRect(rect(), QColor(0x0A, 0x0E, 0x0F));
        painter.drawImage(target, frame_);
    } else {
        QLinearGradient gradient(0, 0, width(), height());
        gradient.setColorAt(0.0, QColor(0x16, 0x29, 0x2B));
        gradient.setColorAt(1.0, QColor(0x0B, 0x11, 0x13));
        painter.fillRect(rect(), gradient);

        QColor ink = SightlineStyle::teal();
        ink.setAlpha(14);
        SightlinePaint::drawHatch(painter, rect(), ink, 12, 4);

        const QColor glyphInk(SightlineStyle::teal().red(), SightlineStyle::teal().green(),
                              SightlineStyle::teal().blue(), 128);
        const QPoint centre(width() / 2, height() / 2 - 8);
        QPolygon triangle;
        triangle << QPoint(centre.x() - 11, centre.y() - 14)
                 << QPoint(centre.x() + 11, centre.y())
                 << QPoint(centre.x() - 11, centre.y() + 14);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(glyphInk);
        painter.setBrush(glyphInk);
        painter.drawPolygon(triangle);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setBrush(Qt::NoBrush);

        painter.setFont(SightlinePaint::monoFont(10));
        painter.setPen(SightlineStyle::faint());
        painter.drawText(QRect(0, centre.y() + 26, width(), 20), Qt::AlignHCenter | Qt::AlignTop,
                         QString::fromUtf8("LIENZO DE VIDEO"));
    }

    if (overlayVisible_ && !overlayLines_.isEmpty()) {
        painter.setFont(SightlinePaint::monoFont(9));
        const QFontMetrics metrics(painter.font());
        int boxWidth = 0;
        for (int i = 0; i < overlayLines_.size(); ++i) {
            const QString line = overlayLines_.at(i);
            boxWidth = qMax(boxWidth, metrics.width(
                line.startsWith(QLatin1Char('>')) ? line.mid(1) : line));
        }

        const int lineHeight = metrics.lineSpacing() + 2;
        const QRect box(10, 10, boxWidth + 22, overlayLines_.size() * lineHeight + 12);
        painter.fillRect(box, QColor(8, 12, 13, 184));
        painter.fillRect(QRect(box.left(), box.top(), 2, box.height()), SightlineStyle::teal());

        for (int i = 0; i < overlayLines_.size(); ++i) {
            const QString line = overlayLines_.at(i);
            const bool highlighted = line.startsWith(QLatin1Char('>'));
            painter.setPen(highlighted ? SightlineStyle::teal() : SightlineStyle::dim());
            painter.drawText(QRect(box.left() + 10, box.top() + 6 + i * lineHeight,
                                   box.width() - 16, lineHeight),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             highlighted ? line.mid(1) : line);
        }
    }

    if (toastVisible_) {
        painter.setFont(SightlinePaint::monoFont(10));
        const QFontMetrics monoMetrics(painter.font());
        painter.setFont(SightlinePaint::uiFont(11));
        const QFontMetrics metrics(painter.font());

        const QString undoText = QString::fromUtf8("Deshacer");
        const int categoryWidth = monoMetrics.width(toastCategory_);
        const int detailWidth = metrics.width(toastDetail_);
        const int undoWidth = metrics.width(undoText);
        const int boxWidth = 11 + categoryWidth + 9 + detailWidth + 9 + undoWidth + 12;
        const int boxHeight = 26;

        const QRect box(10, height() - boxHeight - 10, boxWidth, boxHeight);
        painter.fillRect(box, QColor(8, 12, 13, 230));
        painter.fillRect(QRect(box.left(), box.top(), 3, box.height()), toastColour_);

        int x = box.left() + 11;
        painter.setFont(SightlinePaint::monoFont(10));
        painter.setPen(toastColour_);
        painter.drawText(QRect(x, box.top(), categoryWidth, boxHeight),
                         Qt::AlignVCenter | Qt::AlignLeft, toastCategory_);
        x += categoryWidth + 9;

        painter.setFont(SightlinePaint::uiFont(11));
        painter.setPen(SightlineStyle::text());
        painter.drawText(QRect(x, box.top(), detailWidth, boxHeight),
                         Qt::AlignVCenter | Qt::AlignLeft, toastDetail_);
        x += detailWidth + 9;

        painter.setPen(SightlineStyle::teal());
        undoRect_ = QRect(x, box.top() + 4, undoWidth, boxHeight - 8);
        painter.drawText(undoRect_, Qt::AlignVCenter | Qt::AlignLeft, undoText);
        painter.drawLine(undoRect_.left(), undoRect_.bottom(), undoRect_.right(), undoRect_.bottom());
    } else {
        undoRect_ = QRect();
    }
}

// ================================================================ StatBarRow

StatBarRow::StatBarRow(QWidget *parent)
    : QWidget(parent), rank_(0), fraction_(0.0), top_(false)
{
    setFixedHeight(20);
}

QSize StatBarRow::sizeHint() const
{
    return QSize(200, 20);
}

void StatBarRow::setValues(int rank, const QString &name, const QString &value,
                           double fraction, bool top)
{
    rank_ = rank;
    name_ = name;
    value_ = value;
    fraction_ = qBound(0.0, fraction, 1.0);
    top_ = top;
    update();
}

void StatBarRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    painter.setFont(SightlinePaint::monoFont(10));
    painter.setPen(top_ ? SightlineStyle::teal() : SightlineStyle::faint());
    painter.drawText(QRect(0, 0, 14, height()), Qt::AlignVCenter | Qt::AlignRight,
                     QString::number(rank_));

    painter.setFont(SightlinePaint::uiFont(11));
    painter.setPen(SightlineStyle::text());
    const QFontMetrics metrics(painter.font());
    painter.drawText(QRect(23, 0, 104, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     metrics.elidedText(name_, Qt::ElideRight, 104));

    const int valueWidth = 56;
    const int trackLeft = 136;
    const int trackWidth = qMax(20, width() - valueWidth - 8 - trackLeft);
    const int barTop = (height() - 8) / 2;

    painter.fillRect(QRect(trackLeft, barTop, trackWidth, 8), SightlineStyle::panel());
    painter.fillRect(QRect(trackLeft, barTop, int(trackWidth * fraction_), 8), SightlineStyle::teal());

    painter.setFont(SightlinePaint::monoFont(10));
    const QFontMetrics valueMetrics(painter.font());
    painter.setPen(SightlineStyle::dim());
    painter.drawText(QRect(width() - valueWidth, 0, valueWidth, height()),
                     Qt::AlignVCenter | Qt::AlignRight,
                     valueMetrics.elidedText(value_, Qt::ElideRight, valueWidth));
}

// ============================================================ ListeningClock

ListeningClock::ListeningClock(QWidget *parent)
    : QWidget(parent), currentHour_(-1)
{
    hours_.fill(0, 24);
    setMinimumHeight(88);
}

QSize ListeningClock::sizeHint() const
{
    return QSize(220, 88);
}

void ListeningClock::setHistogram(const QVector<qint64> &hours, int currentHour)
{
    hours_ = hours;
    if (hours_.size() != 24)
        hours_.fill(0, 24);
    currentHour_ = currentHour;
    update();
}

void ListeningClock::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    const int axisHeight = 14;
    const int padding = 10;
    const int chartHeight = height() - axisHeight - 8;
    const int available = width() - padding * 2;
    if (available <= 0 || chartHeight <= 0)
        return;

    qint64 peak = 1;
    for (int i = 0; i < hours_.size(); ++i)
        peak = qMax(peak, hours_.at(i));

    const int gap = 2;
    const int barWidth = qMax(2, (available - gap * 23) / 24);

    for (int hour = 0; hour < 24; ++hour) {
        const double fraction = double(hours_.at(hour)) / double(peak);
        const int barHeight = qMax(1, int(chartHeight * fraction));
        const QRect bar(padding + hour * (barWidth + gap),
                        4 + chartHeight - barHeight, barWidth, barHeight);

        QColor colour = SightlineStyle::tealDim();
        if (fraction > 0.6 || hour == currentHour_)
            colour = SightlineStyle::teal();
        painter.fillRect(bar, colour);

        // The current hour is outlined as well as filled, so the user can
        // see themselves entering their own statistics.
        if (hour == currentHour_) {
            painter.setPen(QColor(SightlineStyle::teal().red(), SightlineStyle::teal().green(),
                                  SightlineStyle::teal().blue(), 110));
            painter.drawRect(bar.adjusted(-1, -1, 0, 0));
        }
    }

    painter.setFont(SightlinePaint::monoFont(9));
    painter.setPen(SightlineStyle::faint());
    const QRect axisRect(padding, height() - axisHeight, available, axisHeight);
    painter.drawText(axisRect, Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1("00"));
    painter.drawText(axisRect, Qt::AlignHCenter | Qt::AlignVCenter, QString::fromLatin1("12"));
    painter.drawText(axisRect, Qt::AlignRight | Qt::AlignVCenter, QString::fromLatin1("23"));
}

// ================================================================ WeekBlocks

WeekBlocks::WeekBlocks(QWidget *parent)
    : QWidget(parent)
{
    weeks_.fill(0, 12);
    setFixedHeight(26);
}

QSize WeekBlocks::sizeHint() const
{
    return QSize(200, 26);
}

void WeekBlocks::setWeeks(const QVector<qint64> &weeks)
{
    weeks_ = weeks;
    if (weeks_.size() != 12)
        weeks_.fill(0, 12);
    update();
}

void WeekBlocks::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    qint64 peak = 1;
    for (int i = 0; i < weeks_.size(); ++i)
        peak = qMax(peak, weeks_.at(i));

    const int gap = 3;
    const int blockWidth = qMax(4, (width() - gap * 11) / 12);

    for (int i = 0; i < 12; ++i) {
        const QRect block(i * (blockWidth + gap), 2, blockWidth, height() - 4);
        const double fraction = double(weeks_.at(i)) / double(peak);

        // Four steps rather than a continuous ramp: at this size a gradient
        // is unreadable, and four levels can actually be told apart.
        QColor fill = SightlineStyle::panel();
        if (fraction > 0.75)
            fill = SightlineStyle::teal();
        else if (fraction > 0.5)
            fill = QColor(47, 191, 174, 184);
        else if (fraction > 0.25)
            fill = QColor(47, 191, 174, 107);
        else if (fraction > 0.0)
            fill = QColor(47, 191, 174, 46);

        painter.fillRect(block, fill);
        painter.setPen(SightlineStyle::line());
        painter.drawRect(block.adjusted(0, 0, -1, -1));
    }
}
