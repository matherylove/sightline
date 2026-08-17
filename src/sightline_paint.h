#ifndef SIGHTLINE_PAINT_H
#define SIGHTLINE_PAINT_H

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>

class QPainter;

// Shared drawing, kept in one place so a thumbnail in the grid, the same
// thumbnail in the recommendation list and the album tile in the music view
// cannot drift apart.
namespace SightlinePaint {

// The diagonal hatch that stands in for artwork we have not downloaded yet.
// It is deliberately a pattern rather than a grey box: a box reads as a
// failure, a pattern reads as "not loaded", which is what it is.
void drawHatch(QPainter &painter, const QRect &rect, const QColor &ink, int spacing = 9, int thickness = 3);

// A placeholder tile whose gradient is derived from the id, so the same
// video is always the same colour and the grid does not read as flat.
QPixmap placeholderTile(const QString &seed, const QSize &size);

// Draws artwork into rect, cropping to fill rather than stretching, and
// falls back to the placeholder when the pixmap is null.
void drawArtwork(QPainter &painter, const QRect &rect, const QPixmap &artwork, const QString &seed);

// The duration chip in the corner of a thumbnail.
void drawDurationChip(QPainter &painter, const QRect &thumbRect, const QString &text);

// The teal resume line along the bottom of a thumbnail. Fraction is 0..1.
void drawResumeBar(QPainter &painter, const QRect &thumbRect, double fraction);

// A one pixel frame in the standard line colour.
void drawFrame(QPainter &painter, const QRect &rect, const QColor &border);

// Elides text to fit and draws it, returning the height used. Word wrapping
// is capped at maxLines so a long title cannot push the metadata out of a card.
int drawWrappedText(QPainter &painter, const QRect &rect, const QString &text, int maxLines);

// Fonts. Everything in the app comes from one of these three so that a
// change of family is a one line edit rather than a search across the tree.
QFont uiFont(int pixelSize, bool bold = false);
QFont monoFont(int pixelSize, bool bold = false);
QFont capsFont(int pixelSize);   // the 9px letterspaced group labels

// A small solid square used as a colour key in legends and settings rows.
QPixmap colourKey(const QColor &colour, int size = 11);

// Formats seconds as 7:32 or 1:04:32, matching VideoItem::durationLabel but
// usable for a live position where there is no VideoItem to hand.
QString clockLabel(double seconds);

// Formats a span of seconds as "68 h 14 min" for the statistics screen.
QString spanLabel(qint64 seconds, bool compact = false);

}

#endif
