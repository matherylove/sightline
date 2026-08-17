#ifndef SIGHTLINE_WIDGETS_H
#define SIGHTLINE_WIDGETS_H

#include <QList>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include "media_types.h"

class QLabel;
class QVBoxLayout;
class Library;

// ---------------------------------------------------------------------------
// The status bar. This is the element that defines the app: the extraction
// chain is fragile by nature, so Sightline shows the whole of it rather than
// pretending it does not exist.
// ---------------------------------------------------------------------------
class SightlineStatusBar : public QWidget
{
    Q_OBJECT

public:
    enum Level { Ready, Working, Degraded };

    explicit SightlineStatusBar(QWidget *parent = 0);

    void setState(Level level, const QString &text);
    void setCell(int index, const QString &text, Level level = Ready);
    void setGrowCell(const QString &text);
    void setTrailing(const QString &text);

protected:
    void paintEvent(QPaintEvent *event);
    QSize sizeHint() const;

private slots:
    void onBlink();

private:
    struct Cell
    {
        Cell() : level(Ready), grow(false) {}
        QString text;
        Level level;
        bool grow;
    };

    QList<Cell> cells_;
    Level level_;
    QString stateText_;
    bool blinkOn_;
};

// ---------------------------------------------------------------------------
// One row in the sidebar: glyph, label, optional count on the right, and a
// 2px teal bar down the left when selected.
// ---------------------------------------------------------------------------
class SidebarItem : public QWidget
{
    Q_OBJECT

public:
    SidebarItem(const QString &glyph, const QString &label, const QString &key, QWidget *parent = 0);

    QString key() const { return key_; }
    void setCount(int count);
    void setSelected(bool selected);
    bool isSelected() const { return selected_; }
    void setFilled(bool filled);      // channels with unwatched videos

signals:
    void activated(const QString &key);

protected:
    void paintEvent(QPaintEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);

private:
    QString glyph_;
    QString label_;
    QString key_;
    int count_;
    bool selected_;
    bool hovered_;
    bool filled_;
};

class SidebarGroup : public QWidget
{
    Q_OBJECT

public:
    explicit SidebarGroup(const QString &title, QWidget *parent = 0);

protected:
    void paintEvent(QPaintEvent *event);

private:
    QString title_;
};

class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = 0);

    void beginRebuild();
    void addGroup(const QString &title);
    SidebarItem *addItem(const QString &glyph, const QString &label, const QString &key,
                         int count = -1, bool filled = false);
    void endRebuild();

    void setSelectedKey(const QString &key);
    QString selectedKey() const { return selected_; }

signals:
    void itemActivated(const QString &key);

private slots:
    void onItemActivated(const QString &key);

private:
    QVBoxLayout *layout_;
    QList<SidebarItem *> items_;
    QString selected_;
};

// ---------------------------------------------------------------------------
// A video card: 16:9 thumbnail with duration chip and resume bar, two lines
// of title, one line of metadata.
// ---------------------------------------------------------------------------
class VideoCard : public QWidget
{
    Q_OBJECT

public:
    explicit VideoCard(QWidget *parent = 0);

    void setVideo(const VideoItem &video);
    void setArtwork(const QPixmap &artwork);
    VideoItem video() const { return video_; }

signals:
    void activated(const QString &videoId);
    void contextRequested(const QString &videoId, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void contextMenuEvent(QContextMenuEvent *event);
    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);

private:
    VideoItem video_;
    QPixmap artwork_;
    bool hovered_;
};

// A grid of VideoCards that reflows between 2 and 4 columns with the width,
// exactly as the mockup does.
class VideoGrid : public QWidget
{
    Q_OBJECT

public:
    explicit VideoGrid(Library *library, QWidget *parent = 0);

    void setVideos(const QList<VideoItem> &videos);
    void clear();
    int count() const { return cards_.size(); }
    void setEmptyText(const QString &text);

signals:
    void videoActivated(const QString &videoId);
    void videoContextRequested(const QString &videoId, const QPoint &globalPos);

public slots:
    void onThumbnailReady(const QString &videoId);

protected:
    void resizeEvent(QResizeEvent *event);
    void paintEvent(QPaintEvent *event);

private:
    void relayout();

    Library *library_;
    QList<VideoCard *> cards_;
    QList<VideoItem> videos_;
    QString emptyText_;
};

// ---------------------------------------------------------------------------
// The seek bar, with SponsorBlock segments drawn into the bar itself rather
// than as a separate strip.
// ---------------------------------------------------------------------------
class SeekBar : public QWidget
{
    Q_OBJECT

public:
    explicit SeekBar(QWidget *parent = 0);

    void setDuration(double seconds);
    void setPosition(double seconds);
    void setBuffered(double seconds);
    void setSegments(const QList<SponsorSegment> &segments);
    void setCompact(bool compact);      // the 3px variant used in the PiP window

    double duration() const { return duration_; }
    double position() const { return position_; }

signals:
    void seekRequested(double seconds);

protected:
    void paintEvent(QPaintEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void leaveEvent(QEvent *event);
    QSize sizeHint() const;

private:
    double positionAt(int x) const;

    double duration_;
    double position_;
    double buffered_;
    QList<SponsorSegment> segments_;
    bool dragging_;
    bool compact_;
};

// ---------------------------------------------------------------------------
// The video canvas. Owns a native child window so a Direct3D 9 device can be
// attached to it later; until a frame arrives it draws the hatch and the
// on-screen readout from the mockup, which is also what shows while the
// first segment is still buffering.
// ---------------------------------------------------------------------------
class VideoSurface : public QWidget
{
    Q_OBJECT

public:
    explicit VideoSurface(QWidget *parent = 0);

    void presentFrame(const QImage &frame);
    void clearFrame();

    void setOverlayLines(const QStringList &lines);
    void setOverlayVisible(bool visible);
    bool overlayVisible() const { return overlayVisible_; }

    void setSkipToast(const QString &category, const QString &detail, const QColor &colour);
    void clearSkipToast();
    bool toastVisible() const { return toastVisible_; }

    // The handle a Direct3D 9 swap chain would be created against.
    WId surfaceHandle() const;

signals:
    void clicked();
    void doubleClicked();
    void undoSkipRequested();
    void resized(const QSize &size);

protected:
    void paintEvent(QPaintEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);
    void resizeEvent(QResizeEvent *event);

private slots:
    void onToastTimeout();

private:
    QRect undoRect() const;

    QImage frame_;
    QStringList overlayLines_;
    bool overlayVisible_;
    bool toastVisible_;
    QString toastCategory_;
    QString toastDetail_;
    QColor toastColour_;
    QRect undoRect_;
};

// ---------------------------------------------------------------------------
// A horizontal bar chart row for the statistics screen.
// ---------------------------------------------------------------------------
class StatBarRow : public QWidget
{
    Q_OBJECT

public:
    explicit StatBarRow(QWidget *parent = 0);

    void setValues(int rank, const QString &name, const QString &value, double fraction, bool top);

protected:
    void paintEvent(QPaintEvent *event);
    QSize sizeHint() const;

private:
    int rank_;
    QString name_;
    QString value_;
    double fraction_;
    bool top_;
};

// The 24 bar listening clock. The bar for the current hour is marked, so the
// user sees themselves entering their own statistics.
class ListeningClock : public QWidget
{
    Q_OBJECT

public:
    explicit ListeningClock(QWidget *parent = 0);

    void setHistogram(const QVector<qint64> &hours, int currentHour);

protected:
    void paintEvent(QPaintEvent *event);
    QSize sizeHint() const;

private:
    QVector<qint64> hours_;
    int currentHour_;
};

// The twelve week block chart.
class WeekBlocks : public QWidget
{
    Q_OBJECT

public:
    explicit WeekBlocks(QWidget *parent = 0);

    void setWeeks(const QVector<qint64> &weeks);

protected:
    void paintEvent(QPaintEvent *event);
    QSize sizeHint() const;

private:
    QVector<qint64> weeks_;
};

#endif
