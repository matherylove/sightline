#ifndef SIGHTLINE_STYLE_H
#define SIGHTLINE_STYLE_H

#include <QColor>
#include <QString>

// Palette and shared stylesheet, taken from design/sightline-ui-mockup.html.
//
// Every value here is a whole pixel. The app targets Windows XP, where the
// system font is Tahoma at 11px and nothing is antialiased into half pixels,
// so the mockup was drawn at that scale and the widgets follow it exactly.
namespace SightlineStyle {

// Chrome
inline QColor voidBg()   { return QColor(0x10, 0x14, 0x16); }
inline QColor panel()    { return QColor(0x1B, 0x21, 0x24); }
inline QColor raise()    { return QColor(0x26, 0x2F, 0x32); }
inline QColor sink()     { return QColor(0x14, 0x19, 0x1B); }
inline QColor line()     { return QColor(0x33, 0x3E, 0x42); }

// Signal
inline QColor teal()     { return QColor(0x2F, 0xBF, 0xAE); }
inline QColor tealDim()  { return QColor(0x17, 0x72, 0x6A); }
inline QColor amber()    { return QColor(0xC9, 0xA2, 0x27); }

// Ink
inline QColor text()     { return QColor(0xC6, 0xD0, 0xD2); }
inline QColor dim()      { return QColor(0x7B, 0x8A, 0x8E); }
inline QColor faint()    { return QColor(0x4E, 0x5D, 0x61); }

// SponsorBlock categories. These are functional, not decorative: five
// segment kinds have to be told apart in a 6px tall bar, so they get their
// own group rather than being forced into the teal/grey scheme.
inline QColor sbSponsor() { return QColor(0xC9, 0xA2, 0x27); }
inline QColor sbIntro()   { return QColor(0x5A, 0x7F, 0xA8); }
inline QColor sbPromo()   { return QColor(0xB0, 0x70, 0x5C); }
inline QColor sbInter()   { return QColor(0x8A, 0x6B, 0xA8); }
inline QColor sbMusic()   { return QColor(0x4E, 0x9E, 0x6A); }

// The interface font. Tahoma ships with every Windows since 95 and is what
// XP itself uses, so it is the honest choice here; the fallbacks only matter
// when the project is built on a developer's Linux box.
inline QString uiFamily()   { return QString::fromLatin1("Tahoma, Verdana, 'DejaVu Sans'"); }

// Figures, timings and the status bar. Consolas is not on XP, so Lucida
// Console leads: it is the XP console font and lines up in columns.
inline QString monoFamily() { return QString::fromLatin1("'Lucida Console', 'Courier New', monospace"); }

inline QString sheet()
{
    return QString::fromLatin1(
        "QWidget { color: #C6D0D2; font-family: Tahoma, Verdana, 'DejaVu Sans'; font-size: 11px; }"
        "QToolTip { background: #262F32; color: #C6D0D2; border: 1px solid #333E42; padding: 3px 6px; }"

        // ---- window shell -------------------------------------------------
        "#appRoot { background: #1B2124; border: 1px solid #333E42; }"
        "#dialogRoot { background: #1B2124; border: 1px solid #333E42; }"
        "#pipRoot { background: #0A0E0F; border: 1px solid #2FBFAE; }"

        "#titleBar { background: #262F32; border-bottom: 1px solid #333E42; }"
        "#titleMark { background: #2FBFAE; }"
        "#titleText { font-weight: bold; font-size: 11px; color: #C6D0D2; background: transparent; }"
        "#titleSuffix { font-size: 11px; color: #4E5D61; background: transparent; }"

        // ---- menu bar -----------------------------------------------------
        "#menuBar { background: transparent; border-bottom: 1px solid #333E42; }"
        "#menuBar::item { background: transparent; color: #C6D0D2; padding: 4px 9px; }"
        "#menuBar::item:selected { background: #17726A; color: #ffffff; }"
        "#menuBar::item:pressed { background: #17726A; color: #ffffff; }"
        "QMenu { background: #1B2124; border: 1px solid #333E42; padding: 3px; }"
        "QMenu::item { padding: 5px 26px 5px 22px; color: #C6D0D2; font-size: 11px; }"
        "QMenu::item:selected { background: #17726A; color: #ffffff; }"
        "QMenu::item:disabled { color: #4E5D61; }"
        "QMenu::separator { height: 1px; background: #333E42; margin: 4px 8px; }"
        "QMenu::indicator { width: 11px; height: 11px; left: 6px; }"

        // ---- sidebar ------------------------------------------------------
        "#sidebar { background: #14191B; border-right: 1px solid #333E42; }"
        "#sideGroup { color: #4E5D61; font-size: 9px; background: transparent; }"

        // ---- toolbar ------------------------------------------------------
        "#toolBar { background: #262F32; border-bottom: 1px solid #333E42; }"
        "#crumbBar { background: transparent; border-bottom: 1px solid #333E42; }"
        "#crumbText { color: #4E5D61; font-size: 11px; background: transparent; }"
        "#crumbStrong { color: #C6D0D2; font-size: 11px; background: transparent; }"

        "QLineEdit { background: #14191B; border: 1px solid #333E42; color: #C6D0D2;"
        "            font-size: 11px; padding: 0 6px; selection-background-color: #17726A; }"
        "QLineEdit:focus { border-color: #2FBFAE; }"

        // ---- buttons ------------------------------------------------------
        "QPushButton { background: #1B2124; border: 1px solid #333E42; color: #7B8A8E;"
        "              font-size: 11px; padding: 0 9px; min-height: 18px; }"
        "QPushButton:hover { background: #262F32; color: #C6D0D2; }"
        "QPushButton:pressed { background: #14191B; }"
        "QPushButton:disabled { color: #4E5D61; background: #1B2124; border-color: #262F32; }"
        "QPushButton:checked { background: #17726A; border-color: #2FBFAE; color: #ffffff; }"
        "#primaryButton { background: #2FBFAE; border: 1px solid #2FBFAE; color: #0C1213; font-weight: bold; }"
        "#primaryButton:hover { background: #46CDBD; color: #0C1213; }"
        "#primaryButton:pressed { background: #17726A; color: #ffffff; }"
        "#primaryButton:disabled { background: #1F3C3A; border-color: #1F3C3A; color: #56706D; }"
        "#subscribeButton { background: #2FBFAE; border: 1px solid #2FBFAE; color: #0C1213; font-weight: bold; padding: 0 12px; }"
        "#subscribeButton:checked { background: transparent; border: 1px solid #2FBFAE; color: #2FBFAE; }"
        "#iconButton { background: #262F32; border: 1px solid #333E42; color: #2FBFAE; padding: 0; }"
        "#iconButton:hover { background: #2E393C; }"
        "#flatLink { background: transparent; border: 0; color: #2FBFAE; font-size: 10px; padding: 0; text-align: right; }"
        "#flatLink:hover { color: #46CDBD; }"

        // ---- tabs ---------------------------------------------------------
        "#paneTab { background: #262F32; border: 0; border-right: 1px solid #333E42;"
        "           color: #7B8A8E; font-size: 10px; padding: 0; min-height: 24px; }"
        "#paneTab:hover { color: #C6D0D2; }"
        "#paneTab:checked { background: #14191B; color: #2FBFAE; border-bottom: 2px solid #2FBFAE; }"
        "#segButton { background: #1B2124; border: 1px solid #333E42; color: #7B8A8E;"
        "             font-size: 10px; padding: 0 10px; min-height: 18px; }"
        "#segButton:checked { background: #17726A; border-color: #2FBFAE; color: #ffffff; }"

        // ---- panes --------------------------------------------------------
        "#sidePane { background: #14191B; border-left: 1px solid #333E42; }"
        "#paneHead { background: transparent; border-bottom: 1px solid #333E42; color: #4E5D61; font-size: 9px; }"
        "#stage { background: #0A0E0F; }"
        "#transport { background: #1B2124; border-top: 1px solid #333E42; }"
        "#videoActions { background: #1B2124; border-top: 1px solid #333E42; }"
        "#boxHead { background: transparent; border-bottom: 1px solid #333E42; color: #4E5D61; font-size: 9px; }"
        "#statBox { background: #14191B; border: 1px solid #333E42; }"

        // ---- scroll areas -------------------------------------------------
        "QAbstractScrollArea { background: #1B2124; border: 0; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QListWidget { background: #1B2124; border: 0; outline: 0; }"
        "QListWidget::item { border: 0; color: #C6D0D2; }"
        "QScrollBar:vertical { background: #14191B; width: 12px; margin: 0; border-left: 1px solid #333E42; }"
        "QScrollBar::handle:vertical { background: #333E42; min-height: 22px; }"
        "QScrollBar::handle:vertical:hover { background: #45535800; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { background: #14191B; height: 12px; margin: 0; border-top: 1px solid #333E42; }"
        "QScrollBar::handle:horizontal { background: #333E42; min-width: 22px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"

        // ---- form controls ------------------------------------------------
        "QCheckBox { color: #7B8A8E; font-size: 11px; spacing: 6px; background: transparent; }"
        "QCheckBox::indicator { width: 11px; height: 11px; border: 1px solid #333E42; background: #14191B; }"
        "QCheckBox::indicator:checked { border-color: #2FBFAE; background: #1F4A47; }"
        "QCheckBox::indicator:disabled { border-color: #262F32; }"
        "QRadioButton { color: #7B8A8E; font-size: 11px; spacing: 6px; background: transparent; }"
        "QRadioButton::indicator { width: 11px; height: 11px; border: 1px solid #333E42; background: #14191B; }"
        "QRadioButton::indicator:checked { border-color: #2FBFAE; background: #2FBFAE; }"
        "QComboBox { background: #14191B; border: 1px solid #333E42; color: #C6D0D2;"
        "            font-size: 11px; padding: 0 6px; min-height: 18px; }"
        "QComboBox:hover { border-color: #45535A; }"
        "QComboBox::drop-down { width: 14px; border: 0; }"
        "QComboBox QAbstractItemView { background: #1B2124; border: 1px solid #333E42;"
        "                              color: #C6D0D2; selection-background-color: #17726A; outline: 0; }"

        // ---- labels -------------------------------------------------------
        "#groupLabel { color: #4E5D61; font-size: 9px; background: transparent; }"
        "#dimLabel { color: #7B8A8E; font-size: 11px; background: transparent; }"
        "#faintLabel { color: #4E5D61; font-size: 10px; background: transparent; }"
        "#tealLabel { color: #2FBFAE; font-size: 11px; background: transparent; }"
        "#headingLabel { color: #C6D0D2; font-size: 13px; font-weight: bold; background: transparent; }"
        "#cardTitle { color: #C6D0D2; font-size: 11px; font-weight: bold; background: transparent; }"
        "#cardMeta { color: #7B8A8E; font-size: 10px; background: transparent; }"
    );
}

}

#endif
