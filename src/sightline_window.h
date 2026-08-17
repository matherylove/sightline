#ifndef SIGHTLINE_WINDOW_H
#define SIGHTLINE_WINDOW_H

#include <QAbstractButton>
#include <QDialog>
#include <QLabel>
#include <QPoint>
#include <QString>
#include <QWidget>

class QVBoxLayout;

// Minimise / maximise / close, drawn with the painter rather than glyphs so
// they land on exact pixels and do not depend on a font having the symbols.
class TitleBarButton : public QAbstractButton
{
    Q_OBJECT

public:
    enum Kind { Minimise, Maximise, Restore, Close };

    explicit TitleBarButton(Kind kind, QWidget *parent = 0);
    void setKind(Kind kind);

protected:
    void paintEvent(QPaintEvent *event);
    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);

private:
    Kind kind_;
    bool hovered_;
};

// The 26px title bar: teal mark, bold title, dim suffix, window buttons.
class SightlineTitleBar : public QWidget
{
    Q_OBJECT

public:
    SightlineTitleBar(const QString &title, bool withMinimise, bool withMaximise, QWidget *parent = 0);

    void setTitle(const QString &title);
    void setSuffix(const QString &suffix);

protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);

private slots:
    void onMinimise();
    void onMaximise();
    void onClose();

private:
    void toggleMaximise();

    QLabel *titleLabel_;
    QLabel *suffixLabel_;
    TitleBarButton *maximiseButton_;
    QPoint dragOffset_;
    bool dragging_;
    bool canMaximise_;
};

// A label that reports clicks. Used for lyric lines and the crumb bar.
class ClickableLabel : public QLabel
{
    Q_OBJECT

public:
    explicit ClickableLabel(QWidget *parent = 0);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *event);
    void enterEvent(QEvent *event);
};

// Frameless dialog sharing the main window's chrome.
class SightlineDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SightlineDialog(const QString &title, QWidget *parent = 0);

    QVBoxLayout *contentLayout() const { return contentLayout_; }
    void setDialogWidth(int width);
    void setDialogTitle(const QString &title);

    static void showMessage(QWidget *parent, const QString &title, const QString &message);
    static bool confirm(QWidget *parent, const QString &title, const QString &message,
                        const QString &acceptText = QString::fromUtf8("Continuar"));

private:
    SightlineTitleBar *titleBar_;
    QVBoxLayout *contentLayout_;
};

// A one pixel horizontal rule in the line colour, for separating rows
// without paying for a QFrame's box model.
class HairLine : public QWidget
{
    Q_OBJECT

public:
    explicit HairLine(QWidget *parent = 0);

protected:
    void paintEvent(QPaintEvent *event);
};

#endif
