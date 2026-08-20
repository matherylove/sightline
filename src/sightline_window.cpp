#include "sightline_window.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "sightline_paint.h"
#include "sightline_style.h"

// ------------------------------------------------------------ TitleBarButton

TitleBarButton::TitleBarButton(Kind kind, QWidget *parent)
    : QAbstractButton(parent), kind_(kind), hovered_(false)
{
    setFixedSize(26, 25);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_Hover, true);

    switch (kind_) {
    case Minimise: setToolTip(QString::fromUtf8("Minimizar")); break;
    case Maximise: setToolTip(QString::fromUtf8("Maximizar")); break;
    case Restore:  setToolTip(QString::fromUtf8("Restaurar")); break;
    case Close:    setToolTip(QString::fromUtf8("Cerrar")); break;
    }
}

void TitleBarButton::setKind(Kind kind)
{
    kind_ = kind;
    setToolTip(kind == Restore ? QString::fromUtf8("Restaurar") : QString::fromUtf8("Maximizar"));
    update();
}

void TitleBarButton::enterEvent(QEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void TitleBarButton::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void TitleBarButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    // The left divider belongs to the button so the row of them reads as one
    // strip whatever order they are added in.
    painter.setPen(SightlineStyle::line());
    painter.drawLine(0, 0, 0, height() - 1);

    if (hovered_ || isDown()) {
        const QColor background = (kind_ == Close)
            ? QColor(0x7A, 0x2F, 0x2F, isDown() ? 255 : 225)
            : QColor(255, 255, 255, isDown() ? 40 : 24);
        painter.fillRect(rect().adjusted(1, 0, 0, 0), background);
    }

    const QColor ink = (hovered_ && kind_ == Close) ? QColor(255, 255, 255) : SightlineStyle::dim();
    painter.setPen(QPen(ink, 1));

    const int side = 9;
    const int left = (width() - side) / 2 + 1;
    const int top = (height() - side) / 2;

    switch (kind_) {
    case Minimise:
        painter.drawLine(left, top + side - 1, left + side - 1, top + side - 1);
        break;
    case Maximise:
        painter.drawRect(left, top, side - 1, side - 1);
        break;
    case Restore:
        painter.drawRect(left, top + 2, side - 3, side - 3);
        painter.drawLine(left + 2, top, left + side - 1, top);
        painter.drawLine(left + side - 1, top, left + side - 1, top + side - 3);
        break;
    case Close:
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawLine(QPointF(left + 0.5, top + 0.5),
                         QPointF(left + side - 0.5, top + side - 0.5));
        painter.drawLine(QPointF(left + side - 0.5, top + 0.5),
                         QPointF(left + 0.5, top + side - 0.5));
        break;
    }
}

// --------------------------------------------------------- SightlineTitleBar

SightlineTitleBar::SightlineTitleBar(const QString &title, bool withMinimise, bool withMaximise, QWidget *parent)
    : QWidget(parent),
      titleLabel_(0),
      suffixLabel_(0),
      maximiseButton_(0),
      dragging_(false),
      canMaximise_(withMaximise)
{
    setObjectName(QString::fromLatin1("titleBar"));
    setFixedHeight(26);
    setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(8);

    QWidget *mark = new QWidget(this);
    mark->setObjectName(QString::fromLatin1("titleMark"));
    mark->setFixedSize(3, 12);
    mark->setAttribute(Qt::WA_StyledBackground, true);
    layout->addWidget(mark);

    titleLabel_ = new QLabel(title, this);
    titleLabel_->setObjectName(QString::fromLatin1("titleText"));
    layout->addWidget(titleLabel_);

    suffixLabel_ = new QLabel(this);
    suffixLabel_->setObjectName(QString::fromLatin1("titleSuffix"));
    suffixLabel_->hide();
    layout->addWidget(suffixLabel_);

    layout->addStretch(1);

    if (withMinimise) {
        TitleBarButton *minimise = new TitleBarButton(TitleBarButton::Minimise, this);
        connect(minimise, SIGNAL(clicked()), this, SLOT(onMinimise()));
        layout->addWidget(minimise);
    }
    if (withMaximise) {
        maximiseButton_ = new TitleBarButton(TitleBarButton::Maximise, this);
        connect(maximiseButton_, SIGNAL(clicked()), this, SLOT(onMaximise()));
        layout->addWidget(maximiseButton_);
    }

    TitleBarButton *close = new TitleBarButton(TitleBarButton::Close, this);
    connect(close, SIGNAL(clicked()), this, SLOT(onClose()));
    layout->addWidget(close);
}

void SightlineTitleBar::setTitle(const QString &title)
{
    titleLabel_->setText(title);
}

void SightlineTitleBar::setSuffix(const QString &suffix)
{
    if (suffix.isEmpty()) {
        suffixLabel_->hide();
        return;
    }
    suffixLabel_->setText(QString::fromUtf8("— ") + suffix);
    suffixLabel_->show();
}

void SightlineTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    QWidget *top = window();
    if (top->isMaximized())
        return;
    dragging_ = true;
    dragOffset_ = event->globalPos() - top->frameGeometry().topLeft();
}

void SightlineTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_ || !(event->buttons() & Qt::LeftButton))
        return;
    window()->move(event->globalPos() - dragOffset_);
}

void SightlineTitleBar::mouseReleaseEvent(QMouseEvent *)
{
    dragging_ = false;
}

void SightlineTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && canMaximise_)
        toggleMaximise();
}

void SightlineTitleBar::onMinimise()
{
    window()->showMinimized();
}

void SightlineTitleBar::onMaximise()
{
    toggleMaximise();
}

void SightlineTitleBar::onClose()
{
    window()->close();
}

void SightlineTitleBar::toggleMaximise()
{
    QWidget *top = window();
    if (top->isMaximized()) {
        top->showNormal();
        if (maximiseButton_)
            maximiseButton_->setKind(TitleBarButton::Maximise);
    } else {
        top->showMaximized();
        if (maximiseButton_)
            maximiseButton_->setKind(TitleBarButton::Restore);
    }
}

// ------------------------------------------------------------ ClickableLabel

ClickableLabel::ClickableLabel(QWidget *parent)
    : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        emit clicked();
    QLabel::mouseReleaseEvent(event);
}

void ClickableLabel::enterEvent(QEvent *event)
{
    QLabel::enterEvent(event);
}

// ----------------------------------------------------------- SightlineDialog

SightlineDialog::SightlineDialog(const QString &title, QWidget *parent)
    : QDialog(parent), titleBar_(0), contentLayout_(0)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setObjectName(QString::fromLatin1("dialogRoot"));
    setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);

    titleBar_ = new SightlineTitleBar(title, false, false, this);
    outer->addWidget(titleBar_);

    QWidget *content = new QWidget(this);
    contentLayout_ = new QVBoxLayout(content);
    contentLayout_->setContentsMargins(18, 16, 18, 14);
    contentLayout_->setSpacing(9);
    outer->addWidget(content, 1);
}

void SightlineDialog::setDialogWidth(int width)
{
    // Fixed across, free down: a dialog whose text wraps needs to be able to
    // grow, and setFixedWidth alone left several of them clipping content.
    setMinimumWidth(width);
    setMaximumWidth(width);
    adjustSize();
}

void SightlineDialog::setDialogTitle(const QString &title)
{
    titleBar_->setTitle(title);
}

void SightlineDialog::showMessage(QWidget *parent, const QString &title, const QString &message)
{
    SightlineDialog dialog(title, parent);
    dialog.setDialogWidth(400);

    QLabel *label = new QLabel(message, &dialog);
    label->setObjectName(QString::fromLatin1("dimLabel"));
    label->setWordWrap(true);
    dialog.contentLayout()->addWidget(label);
    dialog.contentLayout()->addSpacing(6);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    QPushButton *accept = new QPushButton(QString::fromUtf8("Aceptar"), &dialog);
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setFixedHeight(22);
    connect(accept, SIGNAL(clicked()), &dialog, SLOT(accept()));
    buttons->addWidget(accept);
    dialog.contentLayout()->addLayout(buttons);

    dialog.exec();
}

bool SightlineDialog::confirm(QWidget *parent, const QString &title, const QString &message,
                              const QString &acceptText)
{
    SightlineDialog dialog(title, parent);
    dialog.setDialogWidth(400);

    QLabel *label = new QLabel(message, &dialog);
    label->setObjectName(QString::fromLatin1("dimLabel"));
    label->setWordWrap(true);
    dialog.contentLayout()->addWidget(label);
    dialog.contentLayout()->addSpacing(6);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch(1);

    QPushButton *cancel = new QPushButton(QString::fromUtf8("Cancelar"), &dialog);
    cancel->setFixedHeight(22);
    connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    buttons->addWidget(cancel);

    QPushButton *accept = new QPushButton(acceptText, &dialog);
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setFixedHeight(22);
    connect(accept, SIGNAL(clicked()), &dialog, SLOT(accept()));
    buttons->addWidget(accept);

    dialog.contentLayout()->addLayout(buttons);
    return dialog.exec() == QDialog::Accepted;
}

// ------------------------------------------------------------------ HairLine

HairLine::HairLine(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(1);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void HairLine::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), SightlineStyle::line());
}
