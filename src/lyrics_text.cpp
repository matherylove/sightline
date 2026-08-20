#include "lyrics_service.h"

#include <QRegExp>
#include <QStringList>

// The text handling for lyrics, kept apart from the networking so the regular
// expressions can be read without the surrounding machinery.

QString LyricsService::cleanTitle(const QString &title)
{
    QString text = title;

    // Everything YouTube adds to a title and LRCLIB has never heard of.
    // Written as separate passes rather than one clever pattern because each
    // of these fails differently and a single expression hides which one.
    text.remove(QRegExp(QString::fromLatin1(
        "\\((?:[^()]*)(?:official|video|audio|lyrics?|remaster(?:ed)?|hd|hq|4k|8k|mv|"
        "visualizer|performance)(?:[^()]*)\\)"), Qt::CaseInsensitive));

    text.remove(QRegExp(QString::fromLatin1(
        "\\[(?:[^\\[\\]]*)(?:official|video|audio|lyrics?|remaster(?:ed)?|hd|hq|4k|8k|mv)"
        "(?:[^\\[\\]]*)\\]"), Qt::CaseInsensitive));

    // Featured artists confuse the match more often than they help it.
    text.remove(QRegExp(QString::fromLatin1("\\s*[\\(\\[]?\\s*(?:feat|ft)\\.?\\s[^\\)\\]]*[\\)\\]]?\\s*$"),
                        Qt::CaseInsensitive));

    // Trailing junk: "| Official Music Video", "- Topic", stray separators.
    text.remove(QRegExp(QString::fromLatin1("\\s*\\|.*$")));
    text.remove(QRegExp(QString::fromLatin1("\\s*[-\\u2013]\\s*Topic\\s*$"), Qt::CaseInsensitive));

    // "Artist - Title" is the usual shape on YouTube, and the artist is
    // passed to the API separately, so the leading half is dropped.
    const int dash = text.indexOf(QString::fromUtf8(" - "));
    if (dash > 0)
        text = text.mid(dash + 3);

    return text.simplified();
}

QString LyricsService::cleanArtist(const QString &artist)
{
    QString text = artist;

    // Auto-generated music channels are named "<Artist> - Topic", and the
    // VEVO suffix is never part of the name LRCLIB indexed.
    text.remove(QRegExp(QString::fromLatin1("\\s*[-\\u2013]\\s*Topic\\s*$"), Qt::CaseInsensitive));
    text.remove(QRegExp(QString::fromLatin1("VEVO\\s*$"), Qt::CaseInsensitive));
    text.remove(QRegExp(QString::fromLatin1("\\s*[-\\u2013]\\s*Official\\s*$"), Qt::CaseInsensitive));

    return text.simplified();
}

QList<LyricLine> LyricsService::parseLrc(const QString &text, bool *synced)
{
    QList<LyricLine> lines;
    *synced = false;

    // [mm:ss.xx] or [mm:ss:xx]; the fraction is optional and its length says
    // whether it is hundredths or milliseconds.
    QRegExp stamp(QString::fromLatin1("^\\[(\\d+):(\\d+)(?:[.:](\\d+))?\\]"));

    const QStringList rows = text.split(QLatin1Char('\n'));
    for (int i = 0; i < rows.size(); ++i) {
        QString row = rows.at(i);
        row.remove(QLatin1Char('\r'));

        LyricLine line;
        if (stamp.indexIn(row) == 0) {
            const int minutes = stamp.cap(1).toInt();
            const int seconds = stamp.cap(2).toInt();
            const QString fraction = stamp.cap(3);

            double subsecond = 0.0;
            if (!fraction.isEmpty()) {
                subsecond = fraction.toDouble();
                subsecond /= (fraction.size() >= 3) ? 1000.0 : 100.0;
            }

            line.time = minutes * 60 + seconds + subsecond;
            line.text = row.mid(stamp.matchedLength()).trimmed();
            *synced = true;
        } else {
            // Metadata rows such as [ar:Artist] or [length:03:24] are not
            // lyrics and must not appear as blank entries in the panel.
            if (row.startsWith(QLatin1Char('[')))
                continue;
            line.time = -1.0;
            line.text = row.trimmed();
        }

        // Empty lines in an LRC file are usually instrumental gaps. Keeping
        // them as timed blanks lets the highlight sit on nothing during a
        // solo, which is correct, but only when they carry a timestamp.
        if (!line.text.isEmpty() || line.synced())
            lines.append(line);
    }

    return lines;
}
