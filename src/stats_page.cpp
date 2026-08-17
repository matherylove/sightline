#include "stats_page.h"

#include <QButtonGroup>
#include <QDate>
#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "sightline_paint.h"
#include "sightline_style.h"
#include "widgets.h"

// ============================================================ BigNumberCell

BigNumberCell::BigNumberCell(const QString &caption, QWidget *parent)
    : QWidget(parent), caption_(caption)
{
    setMinimumHeight(84);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

QSize BigNumberCell::sizeHint() const
{
    return QSize(160, 84);
}

void BigNumberCell::setValue(const QString &value, const QString &unit)
{
    value_ = value;
    unit_ = unit;
    update();
}

void BigNumberCell::setContext(const QString &plain, const QString &highlighted)
{
    context_ = plain;
    highlight_ = highlighted;
    update();
}

void BigNumberCell::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), SightlineStyle::sink());

    painter.setFont(SightlinePaint::capsFont(9));
    painter.setPen(SightlineStyle::faint());
    painter.drawText(QRect(12, 11, width() - 24, 12), Qt::AlignTop | Qt::AlignLeft, caption_);

    painter.setFont(SightlinePaint::monoFont(27, true));
    painter.setPen(SightlineStyle::teal());
    const QFontMetrics valueMetrics(painter.font());
    painter.drawText(QRect(12, 28, width() - 24, 32), Qt::AlignVCenter | Qt::AlignLeft, value_);

    if (!unit_.isEmpty()) {
        painter.setFont(SightlinePaint::monoFont(13));
        painter.setPen(SightlineStyle::dim());
        painter.drawText(QRect(12 + valueMetrics.width(value_) + 4, 28, 60, 32),
                         Qt::AlignVCenter | Qt::AlignLeft, unit_);
    }

    painter.setFont(SightlinePaint::uiFont(10));
    const QFontMetrics contextMetrics(painter.font());
    int x = 12;
    if (!context_.isEmpty()) {
        painter.setPen(SightlineStyle::dim());
        painter.drawText(QRect(x, 62, width() - 24, 14), Qt::AlignTop | Qt::AlignLeft, context_);
        x += contextMetrics.width(context_);
    }
    if (!highlight_.isEmpty() && x < width() - 20) {
        painter.setPen(SightlineStyle::teal());
        painter.drawText(QRect(x, 62, width() - x - 12, 14), Qt::AlignTop | Qt::AlignLeft,
                         contextMetrics.elidedText(highlight_, Qt::ElideRight, width() - x - 12));
    }
}

// ================================================================ StatsPage

StatsPage::StatsPage(ListeningStats *stats, QWidget *parent)
    : QWidget(parent),
      stats_(stats), period_(ListeningStats::Last30Days),
      heading_(0), liveChip_(0), periodButtons_(0),
      timeCell_(0), tracksCell_(0), artistsCell_(0), streakCell_(0),
      clock_(0), weeks_(0), weeksNote_(0), topTracksLabel_(0),
      detailsLabel_(0), footerLabel_(0)
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);

    // --- heading ----------------------------------------------------------
    QHBoxLayout *headRow = new QHBoxLayout;
    headRow->setSpacing(10);

    heading_ = new QLabel(this);
    heading_->setFont(SightlinePaint::uiFont(14, true));
    headRow->addWidget(heading_);

    liveChip_ = new QLabel(QString::fromUtf8("EN VIVO"), this);
    liveChip_->setFont(SightlinePaint::monoFont(9));
    liveChip_->setStyleSheet(QString::fromLatin1(
        "color: #2FBFAE; border: 1px solid #17726A; padding: 2px 7px;"));
    headRow->addWidget(liveChip_);
    headRow->addStretch(1);

    periodButtons_ = new QButtonGroup(this);
    periodButtons_->setExclusive(true);
    const char *periods[] = { "Hoy", "30 d\xC3\xAD""as", "A\xC3\xB1o", "Siempre" };
    for (int i = 0; i < 4; ++i) {
        QPushButton *button = new QPushButton(QString::fromUtf8(periods[i]), this);
        button->setObjectName(QString::fromLatin1("segButton"));
        button->setCheckable(true);
        button->setChecked(i == int(ListeningStats::Last30Days));
        button->setFixedHeight(20);
        button->setFocusPolicy(Qt::NoFocus);
        periodButtons_->addButton(button, i);
        headRow->addWidget(button);
    }
    connect(periodButtons_, SIGNAL(buttonClicked(int)), this, SLOT(onPeriodClicked(int)));
    root->addLayout(headRow);

    // --- the four figures -------------------------------------------------
    QWidget *numbers = new QWidget(this);
    numbers->setStyleSheet(QString::fromLatin1("background: #333E42;"));
    QHBoxLayout *numbersLayout = new QHBoxLayout(numbers);
    numbersLayout->setContentsMargins(1, 1, 1, 1);
    numbersLayout->setSpacing(1);

    timeCell_ = new BigNumberCell(QString::fromUtf8("Tiempo escuchado"), numbers);
    tracksCell_ = new BigNumberCell(QString::fromUtf8("Pistas"), numbers);
    artistsCell_ = new BigNumberCell(QString::fromUtf8("Artistas"), numbers);
    streakCell_ = new BigNumberCell(QString::fromUtf8("Racha"), numbers);
    numbersLayout->addWidget(timeCell_, 1);
    numbersLayout->addWidget(tracksCell_, 1);
    numbersLayout->addWidget(artistsCell_, 1);
    numbersLayout->addWidget(streakCell_, 1);
    root->addWidget(numbers);

    // --- three columns ----------------------------------------------------
    QHBoxLayout *columns = new QHBoxLayout;
    columns->setSpacing(12);

    QWidget *artistsInner = new QWidget(this);
    QVBoxLayout *artistsLayout = new QVBoxLayout(artistsInner);
    artistsLayout->setContentsMargins(10, 9, 10, 9);
    artistsLayout->setSpacing(0);
    for (int i = 0; i < 7; ++i) {
        StatBarRow *row = new StatBarRow(artistsInner);
        artistRows_.append(row);
        artistsLayout->addWidget(row);
    }
    artistsLayout->addStretch(1);
    columns->addWidget(buildBox(QString::fromUtf8("Top artistas"),
                                QString::fromUtf8("por minutos"), artistsInner), 115);

    QWidget *middle = new QWidget(this);
    QVBoxLayout *middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(12);

    clock_ = new ListeningClock(middle);
    middleLayout->addWidget(buildBox(QString::fromUtf8("Reloj de escucha"),
                                     QString::fromUtf8("hora local"), clock_));

    QWidget *tracksInner = new QWidget(middle);
    QVBoxLayout *tracksLayout = new QVBoxLayout(tracksInner);
    tracksLayout->setContentsMargins(10, 9, 10, 9);
    topTracksLabel_ = new QLabel(tracksInner);
    topTracksLabel_->setFont(SightlinePaint::uiFont(11));
    topTracksLabel_->setObjectName(QString::fromLatin1("dimLabel"));
    topTracksLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    topTracksLabel_->setTextFormat(Qt::RichText);
    tracksLayout->addWidget(topTracksLabel_, 1);
    middleLayout->addWidget(buildBox(QString::fromUtf8("Top pistas"),
                                     QString::fromUtf8("reproducciones"), tracksInner), 1);

    columns->addWidget(middle, 100);

    QWidget *right = new QWidget(this);
    right->setFixedWidth(236);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    QWidget *weeksInner = new QWidget(right);
    QVBoxLayout *weeksLayout = new QVBoxLayout(weeksInner);
    weeksLayout->setContentsMargins(10, 9, 10, 9);
    weeksLayout->setSpacing(6);
    weeks_ = new WeekBlocks(weeksInner);
    weeksLayout->addWidget(weeks_);
    weeksNote_ = new QLabel(weeksInner);
    weeksNote_->setFont(SightlinePaint::monoFont(9));
    weeksNote_->setObjectName(QString::fromLatin1("faintLabel"));
    weeksLayout->addWidget(weeksNote_);
    rightLayout->addWidget(buildBox(QString::fromUtf8("Últimas 12 semanas"), QString(), weeksInner));

    QWidget *detailsInner = new QWidget(right);
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsInner);
    detailsLayout->setContentsMargins(10, 9, 10, 9);
    detailsLabel_ = new QLabel(detailsInner);
    detailsLabel_->setFont(SightlinePaint::monoFont(10));
    detailsLabel_->setTextFormat(Qt::RichText);
    detailsLabel_->setWordWrap(true);
    detailsLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detailsLayout->addWidget(detailsLabel_, 1);
    rightLayout->addWidget(buildBox(QString::fromUtf8("Detalles"), QString(), detailsInner), 1);

    columns->addWidget(right, 0);
    root->addLayout(columns, 1);

    if (stats_)
        connect(stats_, SIGNAL(updated()), this, SLOT(onStatsUpdated()));

    refresh();
}

QWidget *StatsPage::buildBox(const QString &title, const QString &trailing, QWidget *content)
{
    QWidget *box = new QWidget(this);
    box->setObjectName(QString::fromLatin1("statBox"));
    box->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget *head = new QWidget(box);
    head->setObjectName(QString::fromLatin1("boxHead"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    head->setFixedHeight(22);
    QHBoxLayout *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(10, 0, 10, 0);

    QLabel *titleLabel = new QLabel(title, head);
    titleLabel->setFont(SightlinePaint::capsFont(9));
    titleLabel->setObjectName(QString::fromLatin1("groupLabel"));
    headLayout->addWidget(titleLabel);
    headLayout->addStretch(1);

    if (!trailing.isEmpty()) {
        QLabel *trailingLabel = new QLabel(trailing, head);
        trailingLabel->setFont(SightlinePaint::monoFont(9));
        trailingLabel->setObjectName(QString::fromLatin1("faintLabel"));
        headLayout->addWidget(trailingLabel);
    }

    layout->addWidget(head);
    content->setParent(box);
    layout->addWidget(content, 1);
    return box;
}

void StatsPage::onPeriodClicked(int period)
{
    setPeriod(ListeningStats::Period(period));
}

void StatsPage::setPeriod(ListeningStats::Period period)
{
    period_ = period;
    if (periodButtons_->button(int(period)))
        periodButtons_->button(int(period))->setChecked(true);
    refresh();
}

void StatsPage::onStatsUpdated()
{
    if (isVisible())
        refresh();
}

void StatsPage::refresh()
{
    if (!stats_)
        return;

    const ListeningStats::Summary summary = stats_->summarise(period_, true);
    heading_->setText(ListeningStats::periodLabel(period_));
    liveChip_->setVisible(stats_->isPlaying());

    // Time
    const qint64 hours = summary.totalSeconds / 3600;
    const qint64 minutes = (summary.totalSeconds % 3600) / 60;
    if (hours > 0) {
        timeCell_->setValue(QString::number(hours), QString::fromLatin1("h"));
        timeCell_->setContext(QString::fromUtf8("%1 min \xC2\xB7 ").arg(minutes),
                              QString::fromUtf8("+%1 hoy")
                                  .arg(SightlinePaint::spanLabel(summary.todaySeconds, true)));
    } else {
        timeCell_->setValue(QString::number(minutes), QString::fromUtf8("min"));
        timeCell_->setContext(QString(),
                              QString::fromUtf8("+%1 hoy")
                                  .arg(SightlinePaint::spanLabel(summary.todaySeconds, true)));
    }

    tracksCell_->setValue(QString::number(summary.totalPlays));
    const double average = summary.distinctTracks > 0
        ? double(summary.totalPlays) / double(summary.distinctTracks) : 0.0;
    tracksCell_->setContext(QString(),
        QString::fromUtf8("%1 distintas").arg(summary.distinctTracks));
    if (summary.distinctTracks > 0) {
        tracksCell_->setContext(
            QString::fromUtf8("%1 de media \xC2\xB7 ")
                .arg(QString::number(average, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(','))),
            QString::fromUtf8("%1 distintas").arg(summary.distinctTracks));
    }

    artistsCell_->setValue(QString::number(summary.distinctArtists));
    artistsCell_->setContext(QString(),
        QString::fromUtf8("%1 nuevos").arg(summary.newArtists));

    streakCell_->setValue(QString::number(summary.streakDays), QString::fromUtf8("días"));
    streakCell_->setContext(QString::fromUtf8("Tu mejor: "),
        QString::fromUtf8("%1 días").arg(summary.bestStreakDays));

    // Top artists
    const qint64 peak = summary.topArtists.isEmpty() ? 1
        : qMax(qint64(1), summary.topArtists.first().seconds);
    for (int i = 0; i < artistRows_.size(); ++i) {
        if (i < summary.topArtists.size()) {
            const ListeningStats::Entry &entry = summary.topArtists.at(i);
            artistRows_.at(i)->setValues(i + 1, entry.label,
                                         SightlinePaint::spanLabel(entry.seconds, true),
                                         double(entry.seconds) / double(peak), i == 0);
            artistRows_.at(i)->show();
        } else {
            artistRows_.at(i)->hide();
        }
    }

    clock_->setHistogram(summary.hourHistogram, QDateTime::currentDateTime().time().hour());
    weeks_->setWeeks(summary.weekHistogram);
    weeksNote_->setText(QString::fromUtf8("Semana más alta: %1\nMedia: %2")
        .arg(SightlinePaint::spanLabel(summary.weekBestSeconds))
        .arg(SightlinePaint::spanLabel(summary.weekAverageSeconds)));

    QString tracksHtml;
    for (int i = 0; i < summary.topTracks.size(); ++i) {
        const ListeningStats::Entry &entry = summary.topTracks.at(i);
        tracksHtml += QString::fromUtf8(
            "<div style='margin-bottom:5px'>"
            "<span style='color:#4E5D61'>%1</span>&nbsp;&nbsp;"
            "<span style='color:#C6D0D2'>%2</span>"
            "<span style='color:#4E5D61'> &nbsp;%3</span><br>"
            "<span style='color:#4E5D61;margin-left:16px'>%4</span></div>")
            .arg(i + 1)
            .arg(entry.label.toHtmlEscaped())
            .arg(entry.plays)
            .arg(entry.sublabel.toHtmlEscaped());
    }
    if (tracksHtml.isEmpty())
        tracksHtml = QString::fromUtf8("<span style='color:#4E5D61'>Todavía sin datos.</span>");
    topTracksLabel_->setText(tracksHtml);

    const QString albumName = summary.topAlbums.isEmpty()
        ? QString::fromUtf8("\xE2\x80\x94") : summary.topAlbums.first().label;
    const QString bestDay = summary.bestDay.isValid()
        ? summary.bestDay.toString(QString::fromLatin1("d MMM")) : QString::fromUtf8("\xE2\x80\x94");

    detailsLabel_->setText(QString::fromUtf8(
        "<div style='line-height:170%%'>"
        "<span style='color:#7B8A8E'>Álbum más oído</span><br>"
        "<span style='color:#C6D0D2'>%1</span><br><br>"
        "<span style='color:#7B8A8E'>Pista más repetida en un día</span><br>"
        "<span style='color:#C6D0D2'>%2 &middot; %3 veces</span><br><br>"
        "<span style='color:#7B8A8E'>Día más largo</span><br>"
        "<span style='color:#C6D0D2'>%4 &middot; %5</span></div>")
        .arg(albumName.toHtmlEscaped())
        .arg(summary.topTrackTitle.isEmpty()
                 ? QString::fromUtf8("\xE2\x80\x94") : summary.topTrackTitle.toHtmlEscaped())
        .arg(summary.topTrackPlaysInADay)
        .arg(bestDay)
        .arg(SightlinePaint::spanLabel(summary.bestDaySeconds)));
}
