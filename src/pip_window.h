#ifndef SIGHTLINE_PIP_WINDOW_H
#define SIGHTLINE_PIP_WINDOW_H

#include <QPoint>
#include <QWidget>

#include "media_types.h"

class QLabel;
class PlaybackController;
class SeekBar;
class VideoSurface;

// Frameless, always on top, 16:9 locked. Controls appear on hover and the
// window is dragged from anywhere on the picture, since there is no title
// bar to grab. The teal border is the only one in the app, so it can be
// picked out among other windows.
class PipWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PipWindow(PlaybackController *playback, QWidget *parent = 0);

    void setVideo(const VideoItem &video);
    void setSegments(const QList<SponsorSegment> &segments);
    VideoSurface *surface() const { return surface_; }

signals:
    void returnToWindowRequested();

protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void resizeEvent(QResizeEvent *event);
    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);
    void closeEvent(QCloseEvent *event);

private slots:
    void onPositionChanged(double seconds);
    void onPinToggled();

private:
    void setControlsVisible(bool visible);

    PlaybackController *playback_;
    VideoSurface *surface_;
    SeekBar *seek_;
    QWidget *topBar_;
    QWidget *bottomBar_;
    QLabel *titleLabel_;
    QLabel *timeLabel_;

    QPoint dragOffset_;
    bool dragging_;
    bool pinned_;
};

#endif
