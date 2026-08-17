#ifndef SIGHTLINE_STATS_PAGE_H
#define SIGHTLINE_STATS_PAGE_H

#include <QList>
#include <QWidget>

#include "listening_stats.h"

class QButtonGroup;
class QLabel;
class ListeningClock;
class StatBarRow;
class WeekBlocks;

// A big figure cell: caption, mono numeral, one line of context.
class BigNumberCell : public QWidget
{
    Q_OBJECT

public:
    explicit BigNumberCell(const QString &caption, QWidget *parent = 0);

    void setValue(const QString &value, const QString &unit = QString());
    void setContext(const QString &plain, const QString &highlighted = QString());

protected:
    void paintEvent(QPaintEvent *event);
    QSize sizeHint() const;

private:
    QString caption_;
    QString value_;
    QString unit_;
    QString context_;
    QString highlight_;
};

// The live recap. Nothing waits for December: the counters move while the
// track plays, and the panel is a readout rather than a card to share.
class StatsPage : public QWidget
{
    Q_OBJECT

public:
    StatsPage(ListeningStats *stats, QWidget *parent = 0);

    void refresh();
    void setPeriod(ListeningStats::Period period);
    ListeningStats::Period period() const { return period_; }

signals:
    void exportCsvRequested();
    void exportImageRequested();

private slots:
    void onPeriodClicked(int period);
    void onStatsUpdated();

private:
    QWidget *buildBox(const QString &title, const QString &trailing, QWidget *content);

    ListeningStats *stats_;
    ListeningStats::Period period_;

    QLabel *heading_;
    QLabel *liveChip_;
    QButtonGroup *periodButtons_;

    BigNumberCell *timeCell_;
    BigNumberCell *tracksCell_;
    BigNumberCell *artistsCell_;
    BigNumberCell *streakCell_;

    QList<StatBarRow *> artistRows_;
    ListeningClock *clock_;
    WeekBlocks *weeks_;
    QLabel *weeksNote_;
    QLabel *topTracksLabel_;
    QLabel *detailsLabel_;
    QLabel *footerLabel_;
};

#endif
