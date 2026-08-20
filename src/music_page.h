#ifndef SIGHTLINE_MUSIC_PAGE_H
#define SIGHTLINE_MUSIC_PAGE_H

#include <QList>
#include <QPixmap>
#include <QWidget>

#include "media_types.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class Library;
class AlbumTile;
class GlyphButton;
class PlaybackController;
class SeekBar;

// One track row: index, title, artist, duration.
class TrackRow : public QWidget
{
    Q_OBJECT

public:
    explicit TrackRow(QWidget *parent = 0);

    void setTrack(int index, const VideoItem &video, bool playing);
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
    int index_;
    bool playing_;
    bool hovered_;
};

// The synced lyrics panel. Four line states: past, current, next, far. Only
// the current line is teal. Clicking a line seeks to that second.
class LyricsView : public QWidget
{
    Q_OBJECT

public:
    explicit LyricsView(QWidget *parent = 0);

    void setLines(const QList<LyricLine> &lines);
    void setPosition(double seconds);
    void clear();
    bool synced() const { return synced_; }

signals:
    void seekRequested(double seconds);

protected:
    void paintEvent(QPaintEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);

private:
    int lineAt(int y) const;

    QList<LyricLine> lines_;
    int currentIndex_;
    int scrollOffset_;
    bool synced_;
};

// The music view: album header, track list, lyrics panel, now-playing bar.
class MusicPage : public QWidget
{
    Q_OBJECT

public:
    MusicPage(Library *library, PlaybackController *playback, QWidget *parent = 0);

    void setAlbum(const QString &title, const QString &subtitle, const QList<VideoItem> &tracks);
    void setNowPlaying(const VideoItem &video);
    void setLyrics(const QList<LyricLine> &lines);
    void setLyricsVisible(bool visible);
    void setAlbumArtwork(const QPixmap &artwork);

signals:
    void trackActivated(const QString &videoId);
    void downloadRequested();
    void shuffleRequested();

private slots:
    void onPositionChanged(double seconds);
    void onArtworkReady(const QString &videoId);
    void onLyricsToggled();

private:
    Library *library_;
    PlaybackController *playback_;

    AlbumTile *albumTile_;
    AlbumTile *nowTile_;
    GlyphButton *playPause_;
    QLabel *albumTitle_;
    QLabel *albumSubtitle_;
    QLabel *albumKind_;
    QVBoxLayout *trackLayout_;
    QList<TrackRow *> trackRows_;

    LyricsView *lyrics_;
    QWidget *lyricsPanel_;
    QPushButton *lyricsButton_;
    QLabel *lyricsStatus_;

    QLabel *nowTitle_;
    QLabel *nowArtist_;
    QLabel *nowTime_;
    SeekBar *nowSeek_;

    QList<VideoItem> tracks_;
    QString playingId_;
};

#endif
