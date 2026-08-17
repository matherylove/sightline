#include "sightline_paint.h"

#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QTextLayout>

#include "sightline_style.h"

namespace {

// A cheap stable hash. qHash is not stable across Qt versions and the tile
// colour must not change when the app is rebuilt against a newer 5.6 patch.
uint seedHash(const QString &seed)
{
    uint value = 2166136261u;
    for (int i = 0; i < seed.size(); ++i) {
        value ^= uint(seed.at(i).unicode());
        value *= 16777619u;
    }
    return value;
}

} // namespace

namespace SightlinePaint {

void drawHatch(QPainter &painter, const QRect &rect, const QColor &ink, int spacing, int thickness)
{
    if (rect.isEmpty())
        return;

    painter.save();
    painter.setClipRect(rect);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QPen pen(ink, thickness);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);

    // 126 degrees in the mockup; drawn here as a run of parallel lines from
    // the top-left, offset far enough that the diagonal covers the corners.
    const int span = rect.width() + rect.height();
    for (int x = -rect.height(); x < span; x += spacing) {
        painter.drawLine(rect.left() + x, rect.top(),
                         rect.left() + x - rect.height(), rect.bottom());
    }
    painter.restore();
}

QPixmap placeholderTile(const QString &seed, const QSize &size)
{
    if (size.isEmpty())
        return QPixmap();

    QPixmap pixmap(size);
    const uint hash = seedHash(seed);

    // Eight gradients matching the .t1 to .t8 classes in the mockup, chosen
    // by hash so the grid never lines up two identical tiles by accident.
    static const int tints[8][6] = {
        { 0x1E, 0x3A, 0x3B, 0x13, 0x21, 0x22 },
        { 0x2A, 0x2F, 0x38, 0x15, 0x1A, 0x1D },
        { 0x15, 0x30, 0x36, 0x10, 0x1B, 0x1E },
        { 0x33, 0x30, 0x2A, 0x18, 0x17, 0x15 },
        { 0x1A, 0x2B, 0x3A, 0x11, 0x18, 0x20 },
        { 0x2C, 0x24, 0x36, 0x16, 0x12, 0x1C },
        { 0x1E, 0x38, 0x30, 0x12, 0x1D, 0x19 },
        { 0x38, 0x2A, 0x2A, 0x1C, 0x14, 0x14 }
    };
    const int *tint = tints[hash % 8];

    QPainter painter(&pixmap);
    QLinearGradient gradient(0, 0, size.width(), size.height());
    gradient.setColorAt(0.0, QColor(tint[0], tint[1], tint[2]));
    gradient.setColorAt(1.0, QColor(tint[3], tint[4], tint[5]));
    painter.fillRect(QRect(QPoint(0, 0), size), gradient);

    QColor ink = SightlineStyle::teal();
    ink.setAlpha(23);
    drawHatch(painter, QRect(QPoint(0, 0), size), ink);
    painter.end();

    return pixmap;
}

void drawArtwork(QPainter &painter, const QRect &rect, const QPixmap &artwork, const QString &seed)
{
    if (rect.isEmpty())
        return;

    if (artwork.isNull()) {
        painter.drawPixmap(rect, placeholderTile(seed, rect.size()));
        return;
    }

    // Crop to fill: scaling a 4:3 thumbnail into a 16:9 slot by stretching
    // makes every face in the grid look wrong, so the excess is cut instead.
    const QSize scaled = artwork.size().scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
    const QRect source((scaled.width() - rect.width()) / 2 * artwork.width() / qMax(1, scaled.width()),
                       (scaled.height() - rect.height()) / 2 * artwork.height() / qMax(1, scaled.height()),
                       rect.width() * artwork.width() / qMax(1, scaled.width()),
                       rect.height() * artwork.height() / qMax(1, scaled.height()));
    painter.drawPixmap(rect, artwork, source);
}

void drawDurationChip(QPainter &painter, const QRect &thumbRect, const QString &text)
{
    if (text.isEmpty())
        return;

    painter.save();
    painter.setFont(monoFont(9));
    const QFontMetrics metrics(painter.font());
    const int width = metrics.width(text) + 8;
    const int height = metrics.height() + 2;
    const QRect chip(thumbRect.right() - width - 3, thumbRect.bottom() - height - 3, width, height);

    painter.fillRect(chip, QColor(8, 12, 13, 219));
    painter.setPen(SightlineStyle::text());
    painter.drawText(chip, Qt::AlignCenter, text);
    painter.restore();
}

void drawResumeBar(QPainter &painter, const QRect &thumbRect, double fraction)
{
    if (fraction <= 0.0)
        return;
    if (fraction > 1.0)
        fraction = 1.0;
    const int width = int(thumbRect.width() * fraction);
    painter.fillRect(QRect(thumbRect.left(), thumbRect.bottom() - 1, width, 2),
                     SightlineStyle::teal());
}

void drawFrame(QPainter &painter, const QRect &rect, const QColor &border)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));
    painter.restore();
}

int drawWrappedText(QPainter &painter, const QRect &rect, const QString &text, int maxLines)
{
    if (text.isEmpty() || rect.isEmpty())
        return 0;

    QTextLayout layout(text, painter.font());
    layout.beginLayout();

    const QFontMetrics metrics(painter.font());
    const int lineHeight = metrics.lineSpacing();
    int y = rect.top();
    int lines = 0;

    while (lines < maxLines) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(rect.width());

        const bool isLast = (lines == maxLines - 1);
        QString fragment = text.mid(line.textStart(), line.textLength());

        if (isLast) {
            // Peek at whether anything follows; if so this line has to carry
            // the ellipsis, which QTextLayout will not add on its own.
            QTextLine probe = layout.createLine();
            if (probe.isValid()) {
                const QString remainder = text.mid(line.textStart());
                fragment = metrics.elidedText(remainder, Qt::ElideRight, rect.width());
            }
        }

        painter.drawText(QRect(rect.left(), y, rect.width(), lineHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, fragment);
        y += lineHeight;
        ++lines;

        if (isLast)
            break;
    }
    layout.endLayout();
    return y - rect.top();
}

QFont uiFont(int pixelSize, bool bold)
{
    QFont font(QString::fromLatin1("Tahoma"));
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QFont monoFont(int pixelSize, bool bold)
{
    QFont font(QString::fromLatin1("Lucida Console"));
    font.setStyleHint(QFont::TypeWriter);
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    return font;
}

QFont capsFont(int pixelSize)
{
    QFont font(QString::fromLatin1("Tahoma"));
    font.setPixelSize(pixelSize);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    return font;
}

QPixmap colourKey(const QColor &colour, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(colour);
    return pixmap;
}

QString clockLabel(double seconds)
{
    if (seconds < 0.0)
        seconds = 0.0;
    const qint64 whole = qint64(seconds);
    const qint64 hours = whole / 3600;
    const qint64 minutes = (whole % 3600) / 60;
    const qint64 secs = whole % 60;
    if (hours > 0) {
        return QString::fromLatin1("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QString::fromLatin1("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

QString spanLabel(qint64 seconds, bool compact)
{
    if (seconds < 0)
        seconds = 0;
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;

    if (hours == 0) {
        if (compact)
            return QString::fromLatin1("%1m").arg(minutes);
        return QString::fromUtf8("%1 min").arg(minutes);
    }
    if (compact)
        return QString::fromLatin1("%1h %2m").arg(hours).arg(minutes);
    return QString::fromUtf8("%1 h %2 min").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'));
}

}
