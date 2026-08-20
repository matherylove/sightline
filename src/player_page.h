#ifndef SIGHTLINE_PLAYER_PAGE_H
#define SIGHTLINE_PLAYER_PAGE_H

#include <QList>
#include <QString>
#include <QWidget>

#include "media_types.h"

class QButtonGroup;
class QLabel;
class QPushButton;
class QScrollArea;
class QSlider;
class QStackedWidget;
class QVBoxLayout;

class Library;
class PlaybackController;
class SeekBar;
class VideoSurface;

// One entry in the "A continuación" / recommendations list: 76x43 thumbnail,
// two lines of title, one line of metadata.
class RecommendationRow : public QWidget
{
    Q_OBJECT

public:
    explicit RecommendationRow(QWidget *parent = 0);

    void setVideo(const VideoItem &video, bool isNext);
    void setArtwork(const QPixmap &artwork);
    QString videoId() const { return video_.id; }

signals:
    void activated(const QString &videoId);

protected:
    void paintEvent(QPaintEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);

private:
    VideoItem video_;
    QPixmap artwork_;
    bool isNext_;
    bool hovered_;
};

// One comment, threaded one level deep.
class CommentRow : public QWidget
{
    Q_OBJECT

public:
    explicit CommentRow(QWidget *parent = 0);

    void setComment(const VideoComment &comment);
    void setAvatar(const QPixmap &avatar);
    void setWidthHint(int width);
    QString avatarKey() const;

protected:
    void paintEvent(QPaintEvent *event);

private:
    int measuredHeight(int width) const;

    VideoComment comment_;
    QPixmap avatar_;
};

// The player: canvas, transport with segments, channel row with subscribe,
// and the three-tab side pane. Everything lives on one screen.
class PlayerPage : public QWidget
{
    Q_OBJECT

public:
    enum Pane { NextPane = 0, CommentsPane = 1, FormatsPane = 2 };

    PlayerPage(Library *library, PlaybackController *playback, QWidget *parent = 0);

    void setVideo(const VideoItem &video);
    void setRecommendations(const QList<VideoItem> &videos, const QList<VideoItem> &channelVideos);
    void setComments(const QList<VideoComment> &comments);
    void setCommentsLoading(bool loading);
    void setSegments(const QList<SponsorSegment> &segments);
    void setSubscribed(bool subscribed);
    void setChannelAvatar(const QPixmap &avatar);
    void setPlaying(bool playing);
    void setDecoderInfo(const QString &info);
    void setUrlExpiry(const QString &text);

    VideoSurface *surface() const { return surface_; }
    SeekBar *seekBar() const { return seek_; }
    QString currentItag() const { return currentItag_; }
    QString nextVideoId() const;
    bool autoplayEnabled() const;

signals:
    void playRequested(const QString &videoId);
    void previousRequested();
    void subscribeToggled(bool subscribed);
    void downloadRequested();
    void pipRequested();
    void fullscreenRequested();
    void saveToPlaylistRequested();
    void formatSelected(const QString &itag);
    void commentsRequested();
    void backRequested();

private slots:
    void onPaneChanged(int pane);
    void onStepBack();
    void onStepForward();
    void onNextClicked();
    void onVolumeChanged(int value);
    void onCycleRate();
    void onPositionChanged(double seconds);
    void onDurationChanged(double seconds);
    void onBufferedChanged(double seconds);
    void onStateChanged(int state);
    void onSubscribeClicked();
    void onFormatRowClicked();
    void onThumbnailReady(const QString &videoId);

private:
    QWidget *buildTransport();
    QWidget *buildChannelRow();
    QWidget *buildSidePane();
    void rebuildFormats();
    void refreshOverlay();
    void refreshTimeLabel();

    Library *library_;
    PlaybackController *playback_;

    VideoSurface *surface_;
    SeekBar *seek_;
    QLabel *timeLabel_;
    class GlyphButton *playButton_;
    class GlyphButton *rateButton_;
    QSlider *volumeSlider_;

    QLabel *avatar_;
    QLabel *channelName_;
    QLabel *channelMeta_;
    QPushButton *subscribeButton_;
    QPushButton *likeButton_;

    QButtonGroup *paneTabs_;
    QStackedWidget *panes_;
    QVBoxLayout *nextLayout_;
    QVBoxLayout *commentsLayout_;
    QVBoxLayout *formatsLayout_;
    QLabel *commentsHead_;
    QLabel *expiryLabel_;
    QPushButton *autoplayButton_;

    QList<RecommendationRow *> recommendationRows_;
    QList<CommentRow *> commentRows_;
    VideoItem video_;
    QList<VideoItem> recommendations_;
    QString currentItag_;
    QString decoderInfo_;
    bool commentsLoaded_;
};

#endif
