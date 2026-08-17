#include "pip_window.h"

#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "playback.h"
#include "sightline_paint.h"
#include "sightline_style.h"
#include "widgets.h"

namespace {

QPushButton *pipButton(const QString &glyph, const QString &tip, QWidget *parent)
{
    QPushButton *button = new QPushButton(glyph, parent);
    button->setFixedSize(18, 16);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(QString::fromLatin1(
        "QPushButton { background: rgba(38,47,50,230); border: 1px solid #333E42;"
        " color: #7B8A8E; font-size: 9px; padding: 0; }"
        "QPushButton:hover { color: #C6D0D2; background: #2E393C; }"));
    return button;
}

} // namespace

PipWindow::PipWindow(PlaybackController *playback, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      playback_(playback), surface_(0), seek_(0), topBar_(0), bottomBar_(0),
      titleLabel_(0), timeLabel_(0), dragging_(false), pinned_(true)
{
    setObjectName(QString::fromLatin1("pipRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_Hover, true);
    resize(352, 198);
    setMinimumSize(240, 135);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    surface_ = new VideoSurface(this);
    surface_->setOverlayVisible(false);
    root->addWidget(surface_, 1);

    // The bars are children of the surface so they float over the picture
    // rather than stealing height from it.
    topBar_ = new QWidget(surface_);
    topBar_->setStyleSheet(QString::fromLatin1("background: rgba(8,12,13,215);"));
    QHBoxLayout *topLayout = new QHBoxLayout(topBar_);
    topLayout->setContentsMargins(6, 4, 6, 4);
    topLayout->setSpacing(6);

    titleLabel_ = new QLabel(topBar_);
    titleLabel_->setFont(SightlinePaint::uiFont(10, true));
    titleLabel_->setStyleSheet(QString::fromLatin1("color: #C6D0D2; background: transparent;"));
    topLayout->addWidget(titleLabel_, 1);

    QPushButton *restore = pipButton(QString::fromUtf8("\xE2\x87\xB1"),
                                     QString::fromUtf8("Volver a la ventana"), topBar_);
    connect(restore, SIGNAL(clicked()), this, SIGNAL(returnToWindowRequested()));
    topLayout->addWidget(restore);

    QPushButton *pin = pipButton(QString::fromUtf8("\xE2\x9A\xB2"),
                                 QString::fromUtf8("Anclar encima"), topBar_);
    pin->setCheckable(true);
    pin->setChecked(true);
    connect(pin, SIGNAL(clicked()), this, SLOT(onPinToggled()));
    topLayout->addWidget(pin);

    QPushButton *close = pipButton(QString::fromUtf8("\xC3\x97"),
                                   QString::fromUtf8("Cerrar"), topBar_);
    connect(close, SIGNAL(clicked()), this, SLOT(close()));
    topLayout->addWidget(close);

    bottomBar_ = new QWidget(surface_);
    bottomBar_->setStyleSheet(QString::fromLatin1("background: rgba(8,12,13,224);"));
    QVBoxLayout *bottomLayout = new QVBoxLayout(bottomBar_);
    bottomLayout->setContentsMargins(8, 6, 8, 6);
    bottomLayout->setSpacing(6);

    seek_ = new SeekBar(bottomBar_);
    seek_->setCompact(true);
    connect(seek_, SIGNAL(seekRequested(double)), playback_, SLOT(seek(double)));
    bottomLayout->addWidget(seek_);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->setSpacing(6);

    QPushButton *playPause = new QPushButton(QString::fromUtf8("\xE2\x9D\x9A\xE2\x9D\x9A"), bottomBar_);
    playPause->setObjectName(QString::fromLatin1("primaryButton"));
    playPause->setFixedSize(20, 18);
    connect(playPause, SIGNAL(clicked()), playback_, SLOT(togglePause()));
    controls->addWidget(playPause);

    QPushButton *forward = pipButton(QString::fromUtf8("\xE2\x96\xB8\xE2\x96\xB8"),
                                     QString::fromUtf8("Avanzar"), bottomBar_);
    forward->setFixedSize(20, 18);
    controls->addWidget(forward);

    timeLabel_ = new QLabel(QString::fromLatin1("00:00 / 00:00"), bottomBar_);
    timeLabel_->setFont(SightlinePaint::monoFont(9));
    timeLabel_->setStyleSheet(QString::fromLatin1("color: #7B8A8E; background: transparent;"));
    controls->addWidget(timeLabel_);
    controls->addStretch(1);

    bottomLayout->addLayout(controls);

    connect(playback_, SIGNAL(positionChanged(double)), this, SLOT(onPositionChanged(double)));
    connect(surface_, SIGNAL(clicked()), playback_, SLOT(togglePause()));

    setControlsVisible(false);
}

void PipWindow::setVideo(const VideoItem &video)
{
    titleLabel_->setText(video.title);
    seek_->setDuration(double(video.duration));
    setWindowTitle(video.title);
}

void PipWindow::setSegments(const QList<SponsorSegment> &segments)
{
    seek_->setSegments(segments);
}

void PipWindow::onPinToggled()
{
    pinned_ = !pinned_;
    Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint;
    if (pinned_)
        flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    show();
}

void PipWindow::onPositionChanged(double seconds)
{
    seek_->setPosition(seconds);
    timeLabel_->setText(SightlinePaint::clockLabel(seconds)
                        + QString::fromLatin1(" / ")
                        + SightlinePaint::clockLabel(playback_->duration()));
}

void PipWindow::setControlsVisible(bool visible)
{
    topBar_->setVisible(visible);
    bottomBar_->setVisible(visible);
}

void PipWindow::enterEvent(QEvent *event)
{
    setControlsVisible(true);
    QWidget::enterEvent(event);
}

void PipWindow::leaveEvent(QEvent *event)
{
    setControlsVisible(false);
    QWidget::leaveEvent(event);
}

void PipWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // 16:9 is locked by adjusting the height to whatever width the user drags
    // to, rather than fighting them over both dimensions.
    const int wanted = width() * 9 / 16;
    if (qAbs(height() - wanted) > 2)
        resize(width(), wanted);

    if (topBar_)
        topBar_->setGeometry(0, 0, surface_->width(), 24);
    if (bottomBar_)
        bottomBar_->setGeometry(0, surface_->height() - 44, surface_->width(), 44);
}

void PipWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    dragging_ = true;
    dragOffset_ = event->globalPos() - frameGeometry().topLeft();
}

void PipWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_ && (event->buttons() & Qt::LeftButton))
        move(event->globalPos() - dragOffset_);
}

void PipWindow::mouseReleaseEvent(QMouseEvent *)
{
    dragging_ = false;
}

void PipWindow::closeEvent(QCloseEvent *event)
{
    emit returnToWindowRequested();
    event->accept();
}
