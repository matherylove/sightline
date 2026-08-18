#ifndef SIGHTLINE_DIALOGS_H
#define SIGHTLINE_DIALOGS_H

#include <QList>
#include <QString>
#include <QWidget>

#include "app_settings.h"
#include "media_types.h"
#include "sightline_window.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

// The trim strip: a waveform with two draggable handles. The audio drawn is
// what has already been buffered, so the cut can be found by ear rather than
// by guessing at timecodes.
class TrimStrip : public QWidget
{
    Q_OBJECT

public:
    explicit TrimStrip(QWidget *parent = 0);

    void setDuration(double seconds);
    void setWaveform(const QVector<double> &samples);
    void setRange(double start, double end);
    double rangeStart() const { return start_; }
    double rangeEnd() const { return end_; }

signals:
    void rangeChanged(double start, double end);

protected:
    void paintEvent(QPaintEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    QSize sizeHint() const;

private:
    int xForTime(double seconds) const;
    double timeForX(int x) const;

    double duration_;
    double start_;
    double end_;
    QVector<double> samples_;
    int dragHandle_;    // -1 none, 0 start, 1 end
};

// Download with trim. Cuts are done with -ss and -to over the stream, copying
// without re-encoding when both land on a keyframe; when they do not, the
// dialog says so before starting rather than failing afterwards.
class DownloadDialog : public SightlineDialog
{
    Q_OBJECT

public:
    DownloadDialog(const VideoItem &video, const AppSettings &settings,
                   const QList<SponsorSegment> &segments, QWidget *parent = 0);

    QString selectedVideoItag() const;
    QString selectedAudioItag() const;
    QString container() const;
    QString targetDirectory() const;
    double trimStart() const;
    double trimEnd() const;
    bool trimEnabled() const;
    bool audioOnly() const;
    bool embedThumbnail() const;
    bool embedMetadata() const;
    bool embedChapters() const;
    bool writeSubtitles() const;

    // The argument list this dialog's choices produce, ready for QProcess.
    QStringList buildArguments(const QString &cacheDir) const;

private slots:
    void onContentChanged();
    void onRangeChanged(double start, double end);
    void onUseSegments();
    void onBrowse();
    void onMarkHere();

private:
    void refreshEstimate();
    void refreshKeyframeNote();

    VideoItem video_;
    AppSettings settings_;
    QList<SponsorSegment> segments_;

    QCheckBox *bothCheck_;
    QCheckBox *audioOnlyCheck_;
    QCheckBox *videoOnlyCheck_;
    QComboBox *videoFormatBox_;
    QComboBox *audioFormatBox_;
    QComboBox *containerBox_;
    QLineEdit *directoryEdit_;
    QCheckBox *thumbnailCheck_;
    QCheckBox *metadataCheck_;
    QCheckBox *subtitlesCheck_;
    QCheckBox *chaptersCheck_;

    TrimStrip *trim_;
    QLabel *startLabel_;
    QLabel *endLabel_;
    QLabel *estimateLabel_;
    QLabel *keyframeLabel_;
    QPushButton *segmentsButton_;
    QCheckBox *removeSponsors_;
};

// Device Flow. The XP machine never opens a browser and never sees a
// password; the user types a code on their phone.
class LinkAccountDialog : public SightlineDialog
{
    Q_OBJECT

public:
    explicit LinkAccountDialog(QWidget *parent = 0);

    void setUserCode(const QString &code);
    void setExpiry(int secondsRemaining);
    void setVerificationUrl(const QString &url);
    void stopCountdown();

public slots:
    void onCodeReady(const QString &userCode, const QString &verificationUrl, int expiresIn);
    void setStatus(const QString &text);

signals:
    void importCsvRequested();

private slots:
    void onCopyCode();
    void onTick();

private:
    QLabel *codeLabel_;
    QLabel *statusLabel_;
    QLabel *urlLabel_;
    int remaining_;
    bool counting_;
};

// SponsorBlock settings: three actions per category and nothing else.
class SponsorBlockDialog : public SightlineDialog
{
    Q_OBJECT

public:
    SponsorBlockDialog(const AppSettings &settings, int cachedSegments, QWidget *parent = 0);

    AppSettings result() const { return settings_; }

private slots:
    void onActionChanged(int index);

private:
    AppSettings settings_;
    QList<QComboBox *> actionBoxes_;
    QCheckBox *submitCheck_;
    QCheckBox *countCheck_;
    QCheckBox *prefetchCheck_;
    QLineEdit *serverEdit_;
};

// Extraction chain settings: where yt-dlp, qjs and the token provider live.
class ToolsDialog : public SightlineDialog
{
    Q_OBJECT

public:
    ToolsDialog(const AppSettings &settings, const QString &detectedBinary,
                const QString &detectedRuntime, const QString &version, QWidget *parent = 0);

    AppSettings result() const { return settings_; }

private slots:
    void onBrowseBinary();
    void onBrowseRuntime();
    void onAccept();

private:
    AppSettings settings_;
    QLineEdit *binaryEdit_;
    QLineEdit *runtimeEdit_;
    QLineEdit *providerEdit_;
    QLineEdit *downloadEdit_;
    QComboBox *heightBox_;
    QCheckBox *avcOnlyCheck_;
};

#endif
