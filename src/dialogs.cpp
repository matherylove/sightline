#include "dialogs.h"

#include <QtMath>

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "sightline_paint.h"
#include "sightline_style.h"

namespace {

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1("dimLabel"));
    label->setFixedWidth(104);
    return label;
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setFont(SightlinePaint::capsFont(9));
    label->setObjectName(QString::fromLatin1("groupLabel"));
    return label;
}

QLabel *timeBox(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setFont(SightlinePaint::monoFont(10));
    label->setStyleSheet(QString::fromLatin1(
        "background: #14191B; border: 1px solid #333E42; color: #2FBFAE; padding: 2px 7px;"));
    return label;
}

} // namespace

// ================================================================ TrimStrip

TrimStrip::TrimStrip(QWidget *parent)
    : QWidget(parent), duration_(0.0), start_(0.0), end_(0.0), dragHandle_(-1)
{
    setFixedHeight(46);
    setMouseTracking(true);
    setCursor(Qt::SizeHorCursor);
}

QSize TrimStrip::sizeHint() const
{
    return QSize(400, 46);
}

void TrimStrip::setDuration(double seconds)
{
    duration_ = qMax(0.0, seconds);
    if (end_ <= 0.0)
        end_ = duration_;
    update();
}

void TrimStrip::setWaveform(const QVector<double> &samples)
{
    samples_ = samples;
    update();
}

void TrimStrip::setRange(double start, double end)
{
    start_ = qBound(0.0, start, duration_);
    end_ = qBound(start_, end, duration_);
    update();
    emit rangeChanged(start_, end_);
}

int TrimStrip::xForTime(double seconds) const
{
    if (duration_ <= 0.0)
        return 0;
    return int(width() * qBound(0.0, seconds / duration_, 1.0));
}

double TrimStrip::timeForX(int x) const
{
    if (width() <= 0 || duration_ <= 0.0)
        return 0.0;
    return qBound(0.0, double(x) / double(width()) * duration_, duration_);
}

void TrimStrip::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const int startX = xForTime(start_);
    const int endX = xForTime(end_);
    dragHandle_ = (qAbs(event->pos().x() - startX) <= qAbs(event->pos().x() - endX)) ? 0 : 1;
    mouseMoveEvent(event);
}

void TrimStrip::mouseMoveEvent(QMouseEvent *event)
{
    if (dragHandle_ < 0 || !(event->buttons() & Qt::LeftButton))
        return;

    const double time = timeForX(event->pos().x());
    if (dragHandle_ == 0)
        start_ = qMin(time, end_ - 1.0);
    else
        end_ = qMax(time, start_ + 1.0);

    start_ = qBound(0.0, start_, duration_);
    end_ = qBound(0.0, end_, duration_);
    update();
    emit rangeChanged(start_, end_);
}

void TrimStrip::mouseReleaseEvent(QMouseEvent *)
{
    dragHandle_ = -1;
}

void TrimStrip::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), SightlineStyle::sink());
    SightlinePaint::drawFrame(painter, rect(), SightlineStyle::line());

    // Waveform
    const int count = samples_.isEmpty() ? 40 : samples_.size();
    const int barWidth = qMax(1, (width() - 4) / count);
    QColor waveInk = SightlineStyle::faint();
    waveInk.setAlpha(128);

    for (int i = 0; i < count; ++i) {
        double amplitude = 0.5;
        if (!samples_.isEmpty())
            amplitude = qBound(0.05, samples_.at(i), 1.0);
        else {
            // Without decoded audio yet, a deterministic profile keeps the
            // strip legible instead of showing an empty box.
            amplitude = 0.25 + 0.6 * qAbs(qSin(i * 0.7) * qCos(i * 0.23));
        }
        const int barHeight = int((height() - 8) * amplitude);
        painter.fillRect(QRect(2 + i * barWidth, (height() - barHeight) / 2,
                               qMax(1, barWidth - 1), barHeight), waveInk);
    }

    const int startX = xForTime(start_);
    const int endX = xForTime(end_);

    painter.fillRect(QRect(0, 0, startX, height()), QColor(8, 12, 13, 168));
    painter.fillRect(QRect(endX, 0, width() - endX, height()), QColor(8, 12, 13, 168));

    QColor keep = SightlineStyle::teal();
    keep.setAlpha(36);
    painter.fillRect(QRect(startX, 0, endX - startX, height()), keep);

    painter.fillRect(QRect(startX, 0, 2, height()), SightlineStyle::teal());
    painter.fillRect(QRect(endX - 2, 0, 2, height()), SightlineStyle::teal());
    painter.fillRect(QRect(startX - 4, 0, 10, 12), SightlineStyle::teal());
    painter.fillRect(QRect(endX - 6, 0, 10, 12), SightlineStyle::teal());
}

// =========================================================== DownloadDialog

DownloadDialog::DownloadDialog(const VideoItem &video, const AppSettings &settings,
                               const QList<SponsorSegment> &segments, QWidget *parent)
    : SightlineDialog(QString::fromUtf8("Descargar — ") + video.title, parent),
      video_(video), settings_(settings), segments_(segments),
      bothCheck_(0), audioOnlyCheck_(0), videoOnlyCheck_(0),
      videoFormatBox_(0), audioFormatBox_(0), containerBox_(0), directoryEdit_(0),
      thumbnailCheck_(0), metadataCheck_(0), subtitlesCheck_(0), chaptersCheck_(0),
      trim_(0), startLabel_(0), endLabel_(0), estimateLabel_(0),
      keyframeLabel_(0), segmentsButton_(0), removeSponsors_(0)
{
    setDialogWidth(660);
    QVBoxLayout *layout = contentLayout();

    // --- content kind -----------------------------------------------------
    QHBoxLayout *kindRow = new QHBoxLayout;
    kindRow->setSpacing(14);
    kindRow->addWidget(fieldLabel(QString::fromUtf8("Contenido"), this));

    bothCheck_ = new QCheckBox(QString::fromUtf8("Vídeo + audio"), this);
    bothCheck_->setChecked(true);
    audioOnlyCheck_ = new QCheckBox(QString::fromUtf8("Solo audio"), this);
    videoOnlyCheck_ = new QCheckBox(QString::fromUtf8("Solo vídeo"), this);
    connect(bothCheck_, SIGNAL(clicked()), this, SLOT(onContentChanged()));
    connect(audioOnlyCheck_, SIGNAL(clicked()), this, SLOT(onContentChanged()));
    connect(videoOnlyCheck_, SIGNAL(clicked()), this, SLOT(onContentChanged()));

    kindRow->addWidget(bothCheck_);
    kindRow->addWidget(audioOnlyCheck_);
    kindRow->addWidget(videoOnlyCheck_);
    kindRow->addStretch(1);
    layout->addLayout(kindRow);

    // --- formats ----------------------------------------------------------
    QHBoxLayout *videoRow = new QHBoxLayout;
    videoRow->addWidget(fieldLabel(QString::fromUtf8("Formato"), this));
    videoFormatBox_ = new QComboBox(this);
    videoFormatBox_->setFont(SightlinePaint::monoFont(10));
    videoRow->addWidget(videoFormatBox_, 1);
    layout->addLayout(videoRow);

    QHBoxLayout *audioRow = new QHBoxLayout;
    audioRow->addWidget(fieldLabel(QString::fromUtf8("Audio"), this));
    audioFormatBox_ = new QComboBox(this);
    audioFormatBox_->setFont(SightlinePaint::monoFont(10));
    audioRow->addWidget(audioFormatBox_, 1);
    layout->addLayout(audioRow);

    for (int i = 0; i < video_.formats.size(); ++i) {
        const MediaFormat &format = video_.formats.at(i);
        const QString label = QString::fromLatin1("%1 \xC2\xB7 %2 \xC2\xB7 %3 \xC2\xB7 %4")
            .arg(format.itag)
            .arg(format.qualityLabel())
            .arg(format.hasVideo() ? format.videoCodec : format.audioCodec)
            .arg(format.bitrateLabel());
        if (format.hasVideo() && format.isAvc())
            videoFormatBox_->addItem(label, format.itag);
        else if (!format.hasVideo() && format.hasAudio())
            audioFormatBox_->addItem(label, format.itag);
    }
    if (videoFormatBox_->count() == 0)
        videoFormatBox_->addItem(QString::fromUtf8("Sin formatos H.264 disponibles"), QString());
    if (audioFormatBox_->count() == 0)
        audioFormatBox_->addItem(QString::fromUtf8("Sin pista de audio separada"), QString());

    QHBoxLayout *containerRow = new QHBoxLayout;
    containerRow->addWidget(fieldLabel(QString::fromUtf8("Contenedor"), this));
    containerBox_ = new QComboBox(this);
    containerBox_->addItem(QString::fromUtf8("MP4 (sin recodificar)"), QString::fromLatin1("mp4"));
    containerBox_->addItem(QString::fromLatin1("MKV"), QString::fromLatin1("mkv"));
    containerBox_->addItem(QString::fromLatin1("M4A (solo audio)"), QString::fromLatin1("m4a"));
    containerRow->addWidget(containerBox_, 1);
    layout->addLayout(containerRow);

    QHBoxLayout *dirRow = new QHBoxLayout;
    dirRow->addWidget(fieldLabel(QString::fromUtf8("Guardar en"), this));
    directoryEdit_ = new QLineEdit(settings.downloadDirectory, this);
    directoryEdit_->setFont(SightlinePaint::monoFont(10));
    directoryEdit_->setFixedHeight(20);
    dirRow->addWidget(directoryEdit_, 1);
    QPushButton *browse = new QPushButton(QString::fromUtf8("Examinar\xE2\x80\xA6"), this);
    browse->setFixedHeight(20);
    connect(browse, SIGNAL(clicked()), this, SLOT(onBrowse()));
    dirRow->addWidget(browse);
    layout->addLayout(dirRow);

    QHBoxLayout *extrasRow = new QHBoxLayout;
    extrasRow->setSpacing(14);
    extrasRow->addWidget(fieldLabel(QString(), this));
    thumbnailCheck_ = new QCheckBox(QString::fromUtf8("Miniatura"), this);
    thumbnailCheck_->setChecked(settings.embedThumbnail);
    metadataCheck_ = new QCheckBox(QString::fromUtf8("Metadatos"), this);
    metadataCheck_->setChecked(settings.embedMetadata);
    subtitlesCheck_ = new QCheckBox(QString::fromUtf8("Subtítulos"), this);
    subtitlesCheck_->setChecked(settings.writeSubtitles);
    chaptersCheck_ = new QCheckBox(QString::fromUtf8("Capítulos"), this);
    chaptersCheck_->setChecked(settings.embedChapters);
    extrasRow->addWidget(thumbnailCheck_);
    extrasRow->addWidget(metadataCheck_);
    extrasRow->addWidget(subtitlesCheck_);
    extrasRow->addWidget(chaptersCheck_);
    extrasRow->addStretch(1);
    layout->addLayout(extrasRow);

    // --- trim -------------------------------------------------------------
    QHBoxLayout *trimHead = new QHBoxLayout;
    trimHead->addWidget(sectionLabel(QString::fromUtf8("Recorte"), this));
    trimHead->addStretch(1);
    removeSponsors_ = new QCheckBox(
        QString::fromUtf8("Quitar patrocinios del archivo"), this);
    removeSponsors_->setToolTip(QString::fromUtf8(
        "Lo hace el post-procesador de yt-dlp, no el recorte manual."));
    trimHead->addWidget(removeSponsors_);

    segmentsButton_ = new QPushButton(
        QString::fromUtf8("Usar segmentos de SponsorBlock como cortes"), this);
    segmentsButton_->setObjectName(QString::fromLatin1("flatLink"));
    segmentsButton_->setFlat(true);
    segmentsButton_->setEnabled(!segments_.isEmpty());
    connect(segmentsButton_, SIGNAL(clicked()), this, SLOT(onUseSegments()));
    trimHead->addWidget(segmentsButton_);
    layout->addSpacing(6);
    layout->addLayout(trimHead);

    trim_ = new TrimStrip(this);
    trim_->setDuration(double(video_.duration));
    trim_->setRange(0.0, double(video_.duration));
    connect(trim_, SIGNAL(rangeChanged(double, double)),
            this, SLOT(onRangeChanged(double, double)));
    layout->addWidget(trim_);

    QHBoxLayout *trimFoot = new QHBoxLayout;
    trimFoot->setSpacing(8);
    QLabel *fromLabel = new QLabel(QString::fromUtf8("Desde"), this);
    fromLabel->setFont(SightlinePaint::monoFont(10));
    fromLabel->setObjectName(QString::fromLatin1("dimLabel"));
    trimFoot->addWidget(fromLabel);
    startLabel_ = timeBox(QString::fromLatin1("00:00:00.000"), this);
    trimFoot->addWidget(startLabel_);

    QLabel *toLabel = new QLabel(QString::fromUtf8("Hasta"), this);
    toLabel->setFont(SightlinePaint::monoFont(10));
    toLabel->setObjectName(QString::fromLatin1("dimLabel"));
    trimFoot->addWidget(toLabel);
    endLabel_ = timeBox(QString::fromLatin1("00:00:00.000"), this);
    trimFoot->addWidget(endLabel_);

    estimateLabel_ = new QLabel(this);
    estimateLabel_->setFont(SightlinePaint::monoFont(10));
    estimateLabel_->setObjectName(QString::fromLatin1("dimLabel"));
    trimFoot->addWidget(estimateLabel_);
    trimFoot->addStretch(1);

    QPushButton *markHere = new QPushButton(
        QString::fromUtf8("Marcar en la posición actual"), this);
    markHere->setObjectName(QString::fromLatin1("flatLink"));
    markHere->setFlat(true);
    connect(markHere, SIGNAL(clicked()), this, SLOT(onMarkHere()));
    trimFoot->addWidget(markHere);
    layout->addLayout(trimFoot);

    keyframeLabel_ = new QLabel(this);
    keyframeLabel_->setFont(SightlinePaint::monoFont(10));
    keyframeLabel_->setObjectName(QString::fromLatin1("dimLabel"));
    keyframeLabel_->setWordWrap(true);
    layout->addWidget(keyframeLabel_);

    layout->addStretch(1);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch(1);
    QPushButton *queue = new QPushButton(QString::fromUtf8("Añadir a la cola"), this);
    queue->setFixedHeight(22);
    buttons->addWidget(queue);
    QPushButton *cancel = new QPushButton(QString::fromUtf8("Cancelar"), this);
    cancel->setFixedHeight(22);
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    buttons->addWidget(cancel);
    QPushButton *accept = new QPushButton(QString::fromUtf8("Descargar"), this);
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setFixedHeight(22);
    connect(accept, SIGNAL(clicked()), this, SLOT(accept()));
    connect(queue, SIGNAL(clicked()), this, SLOT(accept()));
    buttons->addWidget(accept);
    layout->addSpacing(6);
    layout->addLayout(buttons);

    onRangeChanged(0.0, double(video_.duration));
}

void DownloadDialog::onContentChanged()
{
    QCheckBox *source = qobject_cast<QCheckBox *>(sender());
    bothCheck_->setChecked(source == bothCheck_);
    audioOnlyCheck_->setChecked(source == audioOnlyCheck_);
    videoOnlyCheck_->setChecked(source == videoOnlyCheck_);
    if (!source->isChecked())
        bothCheck_->setChecked(true);

    videoFormatBox_->setEnabled(!audioOnlyCheck_->isChecked());
    audioFormatBox_->setEnabled(!videoOnlyCheck_->isChecked());
    refreshEstimate();
}

void DownloadDialog::onBrowse()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8("Carpeta de descarga"), directoryEdit_->text());
    if (!chosen.isEmpty())
        directoryEdit_->setText(QDir::toNativeSeparators(chosen));
}

void DownloadDialog::onMarkHere()
{
    // Uses the resume position, which is where the player left off, so the
    // cut can be marked without leaving the video.
    const double here = double(video_.resumePosition);
    if (here > trim_->rangeStart())
        trim_->setRange(trim_->rangeStart(), here);
    else
        trim_->setRange(here, trim_->rangeEnd());
}

void DownloadDialog::onUseSegments()
{
    // Trim to the span between the end of the first sponsor and the start of
    // the last one, which is the usable body of most videos.
    double first = 0.0;
    double last = double(video_.duration);
    for (int i = 0; i < segments_.size(); ++i) {
        const SponsorSegment &segment = segments_.at(i);
        if (segment.category != SponsorSegment::Sponsor
            && segment.category != SponsorSegment::Intro
            && segment.category != SponsorSegment::SelfPromo)
            continue;
        if (segment.start <= 2.0 && segment.end > first)
            first = segment.end;
        if (segment.end >= double(video_.duration) - 2.0 && segment.start < last)
            last = segment.start;
    }
    trim_->setRange(first, last);
}

void DownloadDialog::onRangeChanged(double start, double end)
{
    const QString startText = SightlinePaint::clockLabel(start);
    const QString endText = SightlinePaint::clockLabel(end);
    startLabel_->setText(QString::fromLatin1("%1.%2")
        .arg(startText).arg(int((start - qint64(start)) * 1000), 3, 10, QLatin1Char('0')));
    endLabel_->setText(QString::fromLatin1("%1.%2")
        .arg(endText).arg(int((end - qint64(end)) * 1000), 3, 10, QLatin1Char('0')));
    refreshEstimate();
    refreshKeyframeNote();
}

void DownloadDialog::refreshEstimate()
{
    const double span = trim_->rangeEnd() - trim_->rangeStart();
    qint64 bitrate = 0;

    const MediaFormat *videoFormat = video_.formatByItag(selectedVideoItag());
    const MediaFormat *audioFormat = video_.formatByItag(selectedAudioItag());
    if (videoFormat && !audioOnlyCheck_->isChecked())
        bitrate += videoFormat->bitrate;
    if (audioFormat && !videoOnlyCheck_->isChecked())
        bitrate += audioFormat->bitrate;

    const qint64 bytes = qint64(span * bitrate / 8.0);
    estimateLabel_->setText(QString::fromUtf8("= %1 \xC2\xB7 \xE2\x89\x88 %2 MB")
        .arg(SightlinePaint::spanLabel(qint64(span)))
        .arg(bytes / (1024 * 1024)));
}

void DownloadDialog::refreshKeyframeNote()
{
    // Keyframes on YouTube's H.264 renditions land every two seconds or so.
    // Anything within 120 ms of that grid copies cleanly.
    const double start = trim_->rangeStart();
    const double end = trim_->rangeEnd();
    const double gridStart = qAbs(start - qRound(start / 2.0) * 2.0);
    const double gridEnd = qAbs(end - qRound(end / 2.0) * 2.0);

    if (gridStart < 0.12 && gridEnd < 0.12) {
        keyframeLabel_->setText(QString::fromUtf8(
            "Los dos cortes caen en fotograma clave: se copiará sin recodificar."));
    } else {
        keyframeLabel_->setText(QString::fromUtf8(
            "Un corte no cae en fotograma clave: se recodificarán solo los bordes, "
            "no el archivo entero."));
    }
}

QString DownloadDialog::selectedVideoItag() const
{
    return videoFormatBox_->itemData(videoFormatBox_->currentIndex()).toString();
}

QString DownloadDialog::selectedAudioItag() const
{
    return audioFormatBox_->itemData(audioFormatBox_->currentIndex()).toString();
}

QString DownloadDialog::container() const
{
    return containerBox_->itemData(containerBox_->currentIndex()).toString();
}

QString DownloadDialog::targetDirectory() const { return directoryEdit_->text(); }
double DownloadDialog::trimStart() const { return trim_->rangeStart(); }
double DownloadDialog::trimEnd() const { return trim_->rangeEnd(); }
bool DownloadDialog::audioOnly() const { return audioOnlyCheck_->isChecked(); }
bool DownloadDialog::embedThumbnail() const { return thumbnailCheck_->isChecked(); }
bool DownloadDialog::embedMetadata() const { return metadataCheck_->isChecked(); }
bool DownloadDialog::embedChapters() const { return chaptersCheck_->isChecked(); }
bool DownloadDialog::writeSubtitles() const { return subtitlesCheck_->isChecked(); }

bool DownloadDialog::trimEnabled() const
{
    return trim_->rangeStart() > 0.5
        || trim_->rangeEnd() < double(video_.duration) - 0.5;
}

QStringList DownloadDialog::buildArguments(const QString &cacheDir) const
{
    QStringList arguments;
    arguments << QString::fromLatin1("--ignore-config")
              << QString::fromLatin1("--no-warnings")
              << QString::fromLatin1("--no-update")
              << QString::fromLatin1("--no-playlist")
              << QString::fromLatin1("--cache-dir") << QDir::toNativeSeparators(cacheDir);

    // An explicit itag wins, but it always carries a fallback so a format
    // that has since disappeared does not turn into a failed download.
    QString selector;
    if (audioOnly()) {
        selector = selectedAudioItag().isEmpty()
            ? QString::fromLatin1("ba[acodec^=mp4a]/ba")
            : selectedAudioItag() + QString::fromLatin1("/ba[acodec^=mp4a]/ba");
    } else if (videoOnlyCheck_->isChecked()) {
        selector = selectedVideoItag().isEmpty()
            ? QString::fromLatin1("bv*[vcodec^=avc1]/bv*")
            : selectedVideoItag() + QString::fromLatin1("/bv*[vcodec^=avc1]/bv*");
    } else if (selectedVideoItag().isEmpty()) {
        selector = settings_.formatSelector();
    } else {
        selector = selectedVideoItag();
        if (!selectedAudioItag().isEmpty())
            selector += QString::fromLatin1("+") + selectedAudioItag();
        selector += QString::fromLatin1("/") + settings_.formatSelector();
    }
    arguments << QString::fromLatin1("-f") << selector;

    // Cutting the sponsors out of the file is yt-dlp's own post-processor,
    // not something to reimplement around the trim handles.
    if (removeSponsors_ && removeSponsors_->isChecked()) {
        arguments << QString::fromLatin1("--sponsorblock-remove")
                  << QString::fromLatin1("sponsor,selfpromo,interaction,intro,outro");
    }

    if (!audioOnly())
        arguments << QString::fromLatin1("--merge-output-format") << container();

    if (embedThumbnail())  arguments << QString::fromLatin1("--embed-thumbnail");
    if (embedMetadata())   arguments << QString::fromLatin1("--embed-metadata");
    if (embedChapters())   arguments << QString::fromLatin1("--embed-chapters");
    if (writeSubtitles())  arguments << QString::fromLatin1("--write-subs")
                                     << QString::fromLatin1("--sub-langs")
                                     << QString::fromLatin1("es,en");

    if (trimEnabled()) {
        // download-sections cuts at the source rather than fetching the whole
        // file and trimming afterwards, which is the difference between a
        // minute and an hour on a slow connection.
        const QString range = QString::fromLatin1("*%1-%2")
            .arg(QString::number(trimStart(), 'f', 2))
            .arg(QString::number(trimEnd(), 'f', 2));
        arguments << QString::fromLatin1("--download-sections") << range;

        const double gridStart = qAbs(trimStart() - qRound(trimStart() / 2.0) * 2.0);
        const double gridEnd = qAbs(trimEnd() - qRound(trimEnd() / 2.0) * 2.0);
        if (gridStart >= 0.12 || gridEnd >= 0.12)
            arguments << QString::fromLatin1("--force-keyframes-at-cuts");
    }

    arguments << QString::fromLatin1("-o")
              << QDir::toNativeSeparators(targetDirectory()
                     + QString::fromLatin1("/%(title)s [%(id)s].%(ext)s"));
    arguments << video_.watchUrl();
    return arguments;
}

// ========================================================= LinkAccountDialog

LinkAccountDialog::LinkAccountDialog(QWidget *parent)
    : SightlineDialog(QString::fromUtf8("Vincular cuenta de Google"), parent),
      codeLabel_(0), statusLabel_(0), urlLabel_(0), remaining_(900), counting_(false)
{
    setDialogWidth(660);
    QVBoxLayout *layout = contentLayout();

    QLabel *heading = new QLabel(
        QString::fromUtf8("Escribe este código en otro dispositivo"), this);
    heading->setObjectName(QString::fromLatin1("headingLabel"));
    layout->addWidget(heading);

    QLabel *blurb = new QLabel(QString::fromUtf8(
        "Sightline no necesita tu contraseña y no abrirá ningún navegador aquí. "
        "Solo pedirá permiso de lectura para copiar tus suscripciones y listas a este equipo."), this);
    blurb->setObjectName(QString::fromLatin1("dimLabel"));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);
    layout->addSpacing(6);

    QWidget *codeBox = new QWidget(this);
    codeBox->setStyleSheet(QString::fromLatin1(
        "background: #17302E; border: 1px solid #2FBFAE;"));
    QVBoxLayout *codeLayout = new QVBoxLayout(codeBox);
    codeLayout->setContentsMargins(15, 15, 15, 15);
    codeLayout->setSpacing(9);

    QLabel *codeCaption = new QLabel(QString::fromUtf8("Código de vinculación"), codeBox);
    codeCaption->setFont(SightlinePaint::capsFont(9));
    codeCaption->setObjectName(QString::fromLatin1("dimLabel"));
    codeCaption->setAlignment(Qt::AlignCenter);
    codeCaption->setStyleSheet(QString::fromLatin1("border: 0;"));
    codeLayout->addWidget(codeCaption);

    codeLabel_ = new QLabel(QString::fromLatin1("--------"), codeBox);
    QFont codeFont = SightlinePaint::monoFont(27, true);
    codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 4.0);
    codeLabel_->setFont(codeFont);
    codeLabel_->setAlignment(Qt::AlignCenter);
    codeLabel_->setStyleSheet(QString::fromLatin1("color: #2FBFAE; border: 0;"));
    codeLayout->addWidget(codeLabel_);

    layout->addWidget(codeBox);

    urlLabel_ = new QLabel(QString::fromLatin1("google.com/device"), this);
    urlLabel_->setFont(SightlinePaint::monoFont(11));
    urlLabel_->setObjectName(QString::fromLatin1("tealLabel"));
    urlLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(urlLabel_);
    layout->addSpacing(4);

    const char *steps[] = {
        "Abre la direcci\xC3\xB3n de arriba en el m\xC3\xB3vil o en otro ordenador.",
        "Escribe el c\xC3\xB3\x64igo de arriba y acepta el acceso de lectura.",
        "Vuelve aqu\xC3\xAD. Esta ventana se cerrar\xC3\xA1 sola."
    };
    for (int i = 0; i < 3; ++i) {
        QHBoxLayout *step = new QHBoxLayout;
        step->setSpacing(10);
        QLabel *number = new QLabel(QString::number(i + 1), this);
        number->setFont(SightlinePaint::monoFont(10));
        number->setObjectName(QString::fromLatin1("tealLabel"));
        number->setFixedWidth(12);
        step->addWidget(number, 0, Qt::AlignTop);
        QLabel *text = new QLabel(QString::fromUtf8(steps[i]), this);
        text->setObjectName(QString::fromLatin1("dimLabel"));
        text->setWordWrap(true);
        step->addWidget(text, 1);
        layout->addLayout(step);
    }

    // Stating what is not available up front, rather than after the import
    // has finished and the user goes looking for their history.
    QLabel *scope = new QLabel(QString::fromUtf8(
        "Se copiarán: suscripciones, listas propias y «Me gusta».\n"
        "No están disponibles en la API: historial y «Ver más tarde»."), this);
    scope->setFont(SightlinePaint::monoFont(10));
    scope->setObjectName(QString::fromLatin1("faintLabel"));
    layout->addSpacing(4);
    layout->addWidget(scope);

    statusLabel_ = new QLabel(this);
    statusLabel_->setFont(SightlinePaint::monoFont(10));
    statusLabel_->setObjectName(QString::fromLatin1("dimLabel"));
    statusLabel_->setStyleSheet(QString::fromLatin1("border-top: 1px solid #333E42; padding-top: 9px;"));
    layout->addWidget(statusLabel_);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    QPushButton *csv = new QPushButton(QString::fromUtf8("Importar CSV de Takeout"), this);
    csv->setFixedHeight(22);
    connect(csv, SIGNAL(clicked()), this, SIGNAL(importCsvRequested()));
    buttons->addWidget(csv);
    buttons->addStretch(1);
    QPushButton *cancel = new QPushButton(QString::fromUtf8("Cancelar"), this);
    cancel->setFixedHeight(22);
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    buttons->addWidget(cancel);
    QPushButton *copy = new QPushButton(QString::fromUtf8("Copiar código"), this);
    copy->setObjectName(QString::fromLatin1("primaryButton"));
    copy->setFixedHeight(22);
    connect(copy, SIGNAL(clicked()), this, SLOT(onCopyCode()));
    buttons->addWidget(copy);
    layout->addLayout(buttons);

    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(onTick()));
    timer->start(1000);
    onTick();
}

void LinkAccountDialog::setUserCode(const QString &code)
{
    codeLabel_->setText(code);
    counting_ = !code.isEmpty() && !code.startsWith(QLatin1Char('-'));
}

void LinkAccountDialog::setExpiry(int secondsRemaining)
{
    remaining_ = secondsRemaining;
    onTick();
}

void LinkAccountDialog::setStatus(const QString &text)
{
    counting_ = false;
    statusLabel_->setText(text);
}

void LinkAccountDialog::onCodeReady(const QString &userCode, const QString &verificationUrl,
                                    int expiresIn)
{
    setUserCode(userCode);
    setVerificationUrl(verificationUrl);
    remaining_ = expiresIn > 0 ? expiresIn : 900;
    counting_ = true;
}

void LinkAccountDialog::setVerificationUrl(const QString &url)
{
    if (!url.isEmpty())
        urlLabel_->setText(url);
}

void LinkAccountDialog::stopCountdown()
{
    counting_ = false;
}

void LinkAccountDialog::onCopyCode()
{
    QGuiApplication::clipboard()->setText(codeLabel_->text());
}

void LinkAccountDialog::onTick()
{
    // Only counts once a real code is on screen; the polling itself is done
    // by OAuthDevice on Google's own interval, not by this timer.
    if (!counting_)
        return;
    if (remaining_ > 0)
        --remaining_;
    statusLabel_->setText(QString::fromUtf8("Esperando confirmación… el código caduca en %1")
        .arg(SightlinePaint::clockLabel(remaining_)));
}

// ======================================================== SponsorBlockDialog

SponsorBlockDialog::SponsorBlockDialog(const AppSettings &settings, int cachedSegments,
                                       QWidget *parent)
    : SightlineDialog(QString::fromUtf8("SponsorBlock"), parent),
      settings_(settings), submitCheck_(0), countCheck_(0), prefetchCheck_(0), serverEdit_(0)
{
    setDialogWidth(620);
    QVBoxLayout *layout = contentLayout();

    QLabel *blurb = new QLabel(QString::fromUtf8(
        "Cada categoría decide por su cuenta. Tres acciones y nada más: omitir sola, "
        "avisar, o solo marcarse en la barra."), this);
    blurb->setObjectName(QString::fromLatin1("dimLabel"));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);
    layout->addSpacing(4);

    const SponsorSegment::Category categories[] = {
        SponsorSegment::Sponsor, SponsorSegment::Intro, SponsorSegment::SelfPromo,
        SponsorSegment::Interaction, SponsorSegment::MusicOffTopic,
        SponsorSegment::Preview, SponsorSegment::Filler
    };

    for (int i = 0; i < 7; ++i) {
        const SponsorSegment::Category category = categories[i];

        QWidget *row = new QWidget(this);
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 4, 0, 4);
        rowLayout->setSpacing(9);

        QColor key;
        switch (category) {
        case SponsorSegment::Sponsor:       key = SightlineStyle::sbSponsor(); break;
        case SponsorSegment::Intro:         key = SightlineStyle::sbIntro(); break;
        case SponsorSegment::SelfPromo:     key = SightlineStyle::sbPromo(); break;
        case SponsorSegment::Interaction:   key = SightlineStyle::sbInter(); break;
        case SponsorSegment::MusicOffTopic: key = SightlineStyle::sbMusic(); break;
        default:                            key = SightlineStyle::faint(); break;
        }
        QLabel *swatch = new QLabel(row);
        swatch->setFixedSize(11, 11);
        swatch->setPixmap(SightlinePaint::colourKey(key, 11));
        rowLayout->addWidget(swatch, 0, Qt::AlignTop);

        QWidget *text = new QWidget(row);
        QVBoxLayout *textLayout = new QVBoxLayout(text);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(1);
        QLabel *name = new QLabel(SponsorSegment::displayName(category), text);
        textLayout->addWidget(name);
        QLabel *description = new QLabel(SponsorSegment::description(category), text);
        description->setObjectName(QString::fromLatin1("faintLabel"));
        textLayout->addWidget(description);
        rowLayout->addWidget(text, 1);

        QComboBox *action = new QComboBox(row);
        action->addItem(QString::fromUtf8("Omitir solo"), int(SegmentSkipSilently));
        action->addItem(QString::fromUtf8("Avisar"), int(SegmentPrompt));
        action->addItem(QString::fromUtf8("Solo marcar"), int(SegmentMarkOnly));
        action->addItem(QString::fromUtf8("Ignorar"), int(SegmentIgnore));
        action->setCurrentIndex(int(settings_.actionFor(category)));
        action->setFixedWidth(120);
        action->setProperty("category", int(category));
        connect(action, SIGNAL(currentIndexChanged(int)), this, SLOT(onActionChanged(int)));
        actionBoxes_.append(action);
        rowLayout->addWidget(action, 0, Qt::AlignTop);

        layout->addWidget(row);
        layout->addWidget(new HairLine(this));
    }

    submitCheck_ = new QCheckBox(
        QString::fromUtf8("Enviar mis saltos a la comunidad (anónimo)"), this);
    submitCheck_->setChecked(settings_.submitSegments);
    layout->addWidget(submitCheck_);

    countCheck_ = new QCheckBox(
        QString::fromUtf8("Contar el tiempo ahorrado en mis estadísticas"), this);
    countCheck_->setChecked(settings_.countTimeSaved);
    layout->addWidget(countCheck_);

    prefetchCheck_ = new QCheckBox(
        QString::fromUtf8("Descargar segmentos al abrir el vídeo, no al llegar al corte"), this);
    prefetchCheck_->setChecked(settings_.prefetchSegments);
    layout->addWidget(prefetchCheck_);

    QHBoxLayout *serverRow = new QHBoxLayout;
    serverRow->addWidget(fieldLabel(QString::fromUtf8("Servidor"), this));
    serverEdit_ = new QLineEdit(settings_.sponsorBlockServer, this);
    serverEdit_->setFont(SightlinePaint::monoFont(10));
    serverEdit_->setFixedHeight(20);
    serverRow->addWidget(serverEdit_, 1);
    layout->addLayout(serverRow);

    QLabel *summary = new QLabel(QString::fromUtf8(
        "Segmentos guardados: %1 \xC2\xB7 tiempo ahorrado: %2")
        .arg(cachedSegments)
        .arg(SightlinePaint::spanLabel(settings_.secondsSaved)), this);
    summary->setFont(SightlinePaint::monoFont(10));
    summary->setObjectName(QString::fromLatin1("faintLabel"));
    layout->addSpacing(4);
    layout->addWidget(summary);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch(1);
    QPushButton *cancel = new QPushButton(QString::fromUtf8("Cancelar"), this);
    cancel->setFixedHeight(22);
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    buttons->addWidget(cancel);
    QPushButton *accept = new QPushButton(QString::fromUtf8("Guardar"), this);
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setFixedHeight(22);
    connect(accept, SIGNAL(clicked()), this, SLOT(accept()));
    buttons->addWidget(accept);
    layout->addLayout(buttons);
}

void SponsorBlockDialog::onActionChanged(int index)
{
    QComboBox *box = qobject_cast<QComboBox *>(sender());
    if (!box)
        return;
    const int category = box->property("category").toInt();
    settings_.setActionFor(SponsorSegment::Category(category),
                           SegmentAction(box->itemData(index).toInt()));
    settings_.submitSegments = submitCheck_->isChecked();
    settings_.countTimeSaved = countCheck_->isChecked();
    settings_.prefetchSegments = prefetchCheck_->isChecked();
    settings_.sponsorBlockServer = serverEdit_->text();
}

// ============================================================== ToolsDialog

ToolsDialog::ToolsDialog(const AppSettings &settings, const QString &detectedBinary,
                         const QString &detectedRuntime, const QString &version, QWidget *parent)
    : SightlineDialog(QString::fromUtf8("Cadena de extracción"), parent),
      settings_(settings), binaryEdit_(0), runtimeEdit_(0), providerEdit_(0),
      downloadEdit_(0), heightBox_(0), avcOnlyCheck_(0)
{
    setDialogWidth(620);
    QVBoxLayout *layout = contentLayout();

    QLabel *blurb = new QLabel(QString::fromUtf8(
        "Sightline no habla con YouTube directamente: todo pasa por yt-dlp.exe, que se "
        "actualiza reemplazando su carpeta sin recompilar nada."), this);
    blurb->setObjectName(QString::fromLatin1("dimLabel"));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);
    layout->addSpacing(4);

    QHBoxLayout *binaryRow = new QHBoxLayout;
    binaryRow->addWidget(fieldLabel(QString::fromLatin1("yt-dlp.exe"), this));
    binaryEdit_ = new QLineEdit(settings_.ytdlpPath.isEmpty() ? detectedBinary
                                                              : settings_.ytdlpPath, this);
    binaryEdit_->setFont(SightlinePaint::monoFont(10));
    binaryEdit_->setFixedHeight(20);
    binaryEdit_->setPlaceholderText(QString::fromUtf8("Sin detectar — colócalo en tools\\"));
    binaryRow->addWidget(binaryEdit_, 1);
    QPushButton *browseBinary = new QPushButton(QString::fromUtf8("Examinar\xE2\x80\xA6"), this);
    browseBinary->setFixedHeight(20);
    connect(browseBinary, SIGNAL(clicked()), this, SLOT(onBrowseBinary()));
    binaryRow->addWidget(browseBinary);
    layout->addLayout(binaryRow);

    QLabel *versionLabel = new QLabel(version.isEmpty()
        ? QString::fromUtf8("Versión: sin detectar")
        : QString::fromUtf8("Versión: ") + version, this);
    versionLabel->setFont(SightlinePaint::monoFont(10));
    versionLabel->setObjectName(QString::fromLatin1("faintLabel"));
    versionLabel->setContentsMargins(112, 0, 0, 0);
    layout->addWidget(versionLabel);

    QHBoxLayout *runtimeRow = new QHBoxLayout;
    runtimeRow->addWidget(fieldLabel(QString::fromLatin1("qjs.exe"), this));
    runtimeEdit_ = new QLineEdit(settings_.jsRuntimePath.isEmpty() ? detectedRuntime
                                                                  : settings_.jsRuntimePath, this);
    runtimeEdit_->setFont(SightlinePaint::monoFont(10));
    runtimeEdit_->setFixedHeight(20);
    runtimeEdit_->setPlaceholderText(QString::fromUtf8("Opcional — sin él se usa el intérprete interno"));
    runtimeRow->addWidget(runtimeEdit_, 1);
    QPushButton *browseRuntime = new QPushButton(QString::fromUtf8("Examinar\xE2\x80\xA6"), this);
    browseRuntime->setFixedHeight(20);
    connect(browseRuntime, SIGNAL(clicked()), this, SLOT(onBrowseRuntime()));
    runtimeRow->addWidget(browseRuntime);
    layout->addLayout(runtimeRow);

    QLabel *runtimeNote = new QLabel(QString::fromUtf8(
        "Node y Deno no arrancan en XP. QuickJS es C99 y sí compila: es el único "
        "motor JS viable aquí."), this);
    runtimeNote->setFont(SightlinePaint::monoFont(10));
    runtimeNote->setObjectName(QString::fromLatin1("faintLabel"));
    runtimeNote->setWordWrap(true);
    runtimeNote->setContentsMargins(112, 0, 0, 0);
    layout->addWidget(runtimeNote);

    QHBoxLayout *providerRow = new QHBoxLayout;
    providerRow->addWidget(fieldLabel(QString::fromUtf8("Proveedor POT"), this));
    providerEdit_ = new QLineEdit(settings_.potProviderUrl, this);
    providerEdit_->setFont(SightlinePaint::monoFont(10));
    providerEdit_->setFixedHeight(20);
    providerEdit_->setPlaceholderText(QString::fromLatin1("http://192.168.1.40:4416"));
    providerRow->addWidget(providerEdit_, 1);
    layout->addLayout(providerRow);

    QLabel *providerNote = new QLabel(QString::fromUtf8(
        "BotGuard no corre en XP, pero no tiene por qué: levanta bgutil-ytdlp-pot-provider "
        "en cualquier máquina de la red y apunta aquí. Sin él, la calidad queda limitada."), this);
    providerNote->setFont(SightlinePaint::monoFont(10));
    providerNote->setObjectName(QString::fromLatin1("faintLabel"));
    providerNote->setWordWrap(true);
    providerNote->setContentsMargins(112, 0, 0, 0);
    layout->addWidget(providerNote);

    layout->addSpacing(6);
    layout->addWidget(sectionLabel(QString::fromUtf8("Política de códecs"), this));

    QHBoxLayout *heightRow = new QHBoxLayout;
    heightRow->addWidget(fieldLabel(QString::fromUtf8("Resolución máx."), this));
    heightBox_ = new QComboBox(this);
    heightBox_->addItem(QString::fromLatin1("360p"), 360);
    heightBox_->addItem(QString::fromLatin1("480p"), 480);
    heightBox_->addItem(QString::fromLatin1("720p"), 720);
    heightBox_->addItem(QString::fromLatin1("1080p"), 1080);
    heightBox_->addItem(QString::fromUtf8("Sin límite"), 0);
    for (int i = 0; i < heightBox_->count(); ++i) {
        if (heightBox_->itemData(i).toInt() == settings_.maxHeight)
            heightBox_->setCurrentIndex(i);
    }
    heightRow->addWidget(heightBox_, 1);
    layout->addLayout(heightRow);

    avcOnlyCheck_ = new QCheckBox(QString::fromUtf8(
        "Solo H.264 (omitir VP9 y AV1, que XP decodifica por software)"), this);
    avcOnlyCheck_->setChecked(settings_.avcOnly);
    layout->addWidget(avcOnlyCheck_);

    QHBoxLayout *downloadRow = new QHBoxLayout;
    downloadRow->addWidget(fieldLabel(QString::fromUtf8("Descargas"), this));
    downloadEdit_ = new QLineEdit(settings_.downloadDirectory, this);
    downloadEdit_->setFont(SightlinePaint::monoFont(10));
    downloadEdit_->setFixedHeight(20);
    downloadRow->addWidget(downloadEdit_, 1);
    layout->addLayout(downloadRow);

    layout->addStretch(1);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch(1);
    QPushButton *cancel = new QPushButton(QString::fromUtf8("Cancelar"), this);
    cancel->setFixedHeight(22);
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    buttons->addWidget(cancel);
    QPushButton *accept = new QPushButton(QString::fromUtf8("Guardar"), this);
    accept->setObjectName(QString::fromLatin1("primaryButton"));
    accept->setFixedHeight(22);
    connect(accept, SIGNAL(clicked()), this, SLOT(onAccept()));
    buttons->addWidget(accept);
    layout->addLayout(buttons);
}

void ToolsDialog::onBrowseBinary()
{
    const QString chosen = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("Selecciona yt-dlp.exe"), binaryEdit_->text(),
        QString::fromUtf8("Ejecutables (*.exe);;Todos los archivos (*)"));
    if (!chosen.isEmpty())
        binaryEdit_->setText(QDir::toNativeSeparators(chosen));
}

void ToolsDialog::onBrowseRuntime()
{
    const QString chosen = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("Selecciona qjs.exe"), runtimeEdit_->text(),
        QString::fromUtf8("Ejecutables (*.exe);;Todos los archivos (*)"));
    if (!chosen.isEmpty())
        runtimeEdit_->setText(QDir::toNativeSeparators(chosen));
}

void ToolsDialog::onAccept()
{
    settings_.ytdlpPath = binaryEdit_->text().trimmed();
    settings_.jsRuntimePath = runtimeEdit_->text().trimmed();
    settings_.potProviderUrl = providerEdit_->text().trimmed();
    settings_.downloadDirectory = downloadEdit_->text().trimmed();
    settings_.maxHeight = heightBox_->itemData(heightBox_->currentIndex()).toInt();
    settings_.avcOnly = avcOnlyCheck_->isChecked();
    accept();
}
