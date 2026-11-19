#include "theme.h"

namespace Theme {

ThemeColors colors(bool isLight)
{
    ThemeColors c;
    c.slateBlue = QStringLiteral("#5576B2");
    c.slateAccent = QStringLiteral("#7695CD");
    c.salmon = QStringLiteral("#F5897C");
    if (isLight) {
        c.pageBg = QStringLiteral("#F1F5F9");
        c.cardBg = QStringLiteral("#FFFFFF");
        c.innerBg = QStringLiteral("#F8FAFC");
        c.border = QStringLiteral("#CBD5E1");
        c.textPrimary = QStringLiteral("#1C2B45");
        c.textSecondary = QStringLiteral("#4E6178");
        c.headerBg = QStringLiteral("#EEF3FA");
        c.headerText = QStringLiteral("#3E5B94");
        c.tableAltBg = QStringLiteral("#F8FAFC");
        c.selectBg = QStringLiteral("#DCE7F7");
        c.successBg = QStringLiteral("#ECFDF5");
        c.successFg = QStringLiteral("#047857");
        c.warnBg = QStringLiteral("#FFFBEB");
        c.warnFg = QStringLiteral("#B45309");
        c.dangerBg = QStringLiteral("#FEF2F2");
        c.dangerFg = QStringLiteral("#B91C1C");
    } else {
        c.pageBg = QStringLiteral("#080B15");
        c.cardBg = QStringLiteral("#0D1321");
        c.innerBg = QStringLiteral("#131B2E");
        c.border = QStringLiteral("#1E293B");
        c.textPrimary = QStringLiteral("#F8FAFC");
        c.textSecondary = QStringLiteral("#94A3B8");
        c.headerBg = QStringLiteral("#1E3052");
        c.headerText = QStringLiteral("#F8FAFC");
        c.tableAltBg = QStringLiteral("#101829");
        c.selectBg = QStringLiteral("#213458");
        c.successBg = QStringLiteral("#0E2923");
        c.successFg = QStringLiteral("#34D399");
        c.warnBg = QStringLiteral("#2B2112");
        c.warnFg = QStringLiteral("#FBBF24");
        c.dangerBg = QStringLiteral("#3B1D24");
        c.dangerFg = QStringLiteral("#FCA5A5");
    }
    return c;
}

QString buildStylesheet(bool isLight)
{
    const ThemeColors c = colors(isLight);
    const QString btnSubtleBg = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#1A2640");
    const QString btnSubtleHover = isLight ? QStringLiteral("#DCE7F7") : QStringLiteral("#263859");
    const QString logBg = isLight ? QStringLiteral("#0F172A") : QStringLiteral("#060911");
    const QString logFg = QStringLiteral("#E2E8F0");

    return QStringLiteral(
        "QMainWindow, QDialog { background-color: %1; color: %5; }"
        "QWidget { color: %5; font-family: 'Inter', 'Noto Sans', 'Segoe UI', sans-serif; font-size: 13px; }"
        "QFrame#headerCard, QFrame#kpiCard, QFrame#inspectorCard, QFrame#toolbarCard { "
        "  background-color: %2; border: 1px solid %4; border-radius: 10px; "
        "}"
        "QFrame#kpiCard { background-color: %3; }"
        "QFrame#warningBanner { background-color: %14; border: 1px solid %15; border-radius: 8px; }"
        "QLabel#appTitle { font-size: 18px; font-weight: 700; color: %5; }"
        "QLabel#appSubtitle { font-size: 12px; color: %6; }"
        "QLabel#kpiTitle { font-size: 11px; font-weight: 700; color: %6; letter-spacing: 0.4px; }"
        "QLabel#kpiValue { font-size: 15px; font-weight: 700; color: %5; }"
        "QLabel#kpiSub { font-size: 11px; color: %6; }"
        "QLabel#mutedLabel { color: %6; font-size: 12px; }"
        "QLabel#warnLabel { color: %15; font-size: 12px; font-weight: 600; }"
        "QPushButton { "
        "  background-color: %10; color: %5; border: 1px solid %4; "
        "  border-radius: 6px; padding: 6px 12px; font-weight: 600; "
        "}"
        "QPushButton:hover { background-color: %11; border-color: %8; }"
        "QPushButton:disabled { opacity: 0.45; }"
        "QPushButton[variant='primary'] { background-color: %7; color: #FFFFFF; border: 1px solid %8; }"
        "QPushButton[variant='primary']:hover { background-color: #4A679E; border-color: %9; }"
        "QPushButton[variant='danger'] { background-color: #C94A3E; color: #FFFFFF; border: 1px solid #A8382E; }"
        "QPushButton[variant='danger']:hover { background-color: #A8382E; }"
        "QPushButton[variant='salmon'] { background-color: %9; color: #080B15; border: 1px solid %9; font-weight: 700; }"
        "QPushButton[variant='salmon']:hover { background-color: #E07466; }"
        "QPushButton[variant='pill'] { border-radius: 14px; padding: 4px 12px; font-size: 12px; }"
        "QPushButton[variant='pill'][active='true'] { background-color: %7; color: #FFFFFF; border-color: %8; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { "
        "  background-color: %3; color: %5; border: 1px solid %4; border-radius: 6px; padding: 5px 8px; "
        "}"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: %8; }"
        "QComboBox QAbstractItemView { background-color: %2; color: %5; border: 1px solid %4; selection-background-color: %7; selection-color: #FFFFFF; }"
        "QTableWidget { "
        "  background-color: %2; alternate-background-color: %12; color: %5; "
        "  border: 1px solid %4; border-radius: 8px; gridline-color: %4; selection-background-color: %13; selection-color: %5; "
        "}"
        "QTableWidget::item { padding: 4px 8px; }"
        "QHeaderView::section { "
        "  background-color: %16; color: %17; border: none; border-bottom: 1px solid %4; "
        "  border-right: 1px solid %4; padding: 7px 8px; font-weight: 700; font-size: 12px; "
        "}"
        "QPlainTextEdit#logViewer { "
        "  background-color: %18; color: %19; border: 1px solid %4; border-radius: 6px; "
        "  font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace; font-size: 12px; padding: 6px; "
        "}"
        "QMenu { background-color: %2; color: %5; border: 1px solid %4; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 6px 14px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: %7; color: #FFFFFF; }"
        "QToolTip { background-color: %3; color: %5; border: 1px solid %8; padding: 5px 8px; }"
        "QSplitter::handle { background-color: %4; height: 4px; border-radius: 2px; }"
    )
        .arg(c.pageBg)        // %1
        .arg(c.cardBg)        // %2
        .arg(c.innerBg)       // %3
        .arg(c.border)        // %4
        .arg(c.textPrimary)   // %5
        .arg(c.textSecondary) // %6
        .arg(c.slateBlue)     // %7
        .arg(c.slateAccent)   // %8
        .arg(c.salmon)        // %9
        .arg(btnSubtleBg)     // %10
        .arg(btnSubtleHover)  // %11
        .arg(c.tableAltBg)    // %12
        .arg(c.selectBg)      // %13
        .arg(c.warnBg)        // %14
        .arg(c.warnFg)        // %15
        .arg(c.headerBg)      // %16
        .arg(c.headerText)    // %17
        .arg(logBg)           // %18
        .arg(logFg);          // %19
}

} // namespace Theme
