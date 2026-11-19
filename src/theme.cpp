#include "theme.h"

#include <QSize>

namespace Theme {

ThemeColors colors(bool isLight)
{
    ThemeColors c;
    if (isLight) {
        c.slateBlue = QStringLiteral("#5576B2");
        c.slateAccent = QStringLiteral("#7695CD");
        c.salmon = QStringLiteral("#E87B6E");
        c.pageBg = QStringLiteral("#EEF2F7");
        c.cardBg = QStringLiteral("#FFFFFF");
        c.innerBg = QStringLiteral("#F5F8FC");
        c.border = QStringLiteral("#D4DFEE");
        c.subtleBorder = QStringLiteral("#E5ECF5");
        c.textPrimary = QStringLiteral("#1B2A44");
        c.textSecondary = QStringLiteral("#546885");
        c.headerBg = QStringLiteral("#EDF3FB");
        c.headerText = QStringLiteral("#3B578C");
        c.tableAltBg = QStringLiteral("#F8FAFD");
        c.selectBg = QStringLiteral("#DCE8FA");
        c.successBg = QStringLiteral("#ECFDF5");
        c.successFg = QStringLiteral("#18795B");
        c.warnBg = QStringLiteral("#FEF8EE");
        c.warnBorder = QStringLiteral("#EED2A5");
        c.warnFg = QStringLiteral("#9A5812");
        c.dangerBg = QStringLiteral("#FDF2F1");
        c.dangerBorder = QStringLiteral("#F0B9B1");
        c.dangerFg = QStringLiteral("#C2453B");
        c.logBg = QStringLiteral("#F5F8FC");
        c.logFg = QStringLiteral("#1E2E48");
    } else {
        c.slateBlue = QStringLiteral("#5B7EC0");
        c.slateAccent = QStringLiteral("#82A3DB");
        c.salmon = QStringLiteral("#F28C80");
        c.pageBg = QStringLiteral("#0A0F1C");
        c.cardBg = QStringLiteral("#111829");
        c.innerBg = QStringLiteral("#172136");
        c.border = QStringLiteral("#23324F");
        c.subtleBorder = QStringLiteral("#1A2640");
        c.textPrimary = QStringLiteral("#EDF2FA");
        c.textSecondary = QStringLiteral("#8FA4C2");
        c.headerBg = QStringLiteral("#192844");
        c.headerText = QStringLiteral("#DCE7FA");
        c.tableAltBg = QStringLiteral("#141D31");
        c.selectBg = QStringLiteral("#22365C");
        c.successBg = QStringLiteral("#0E2923");
        c.successFg = QStringLiteral("#3DD6A4");
        c.warnBg = QStringLiteral("#282012");
        c.warnBorder = QStringLiteral("#5E4823");
        c.warnFg = QStringLiteral("#F2BE5C");
        c.dangerBg = QStringLiteral("#331B20");
        c.dangerBorder = QStringLiteral("#7A3C43");
        c.dangerFg = QStringLiteral("#F59B92");
        c.logBg = QStringLiteral("#0B1120");
        c.logFg = QStringLiteral("#D8E4F5");
    }
    return c;
}

QString buildStylesheet(bool isLight)
{
    const ThemeColors c = colors(isLight);
    const QString chevronIcon = isLight
        ? QStringLiteral(":/icons/chevron_down_light.png")
        : QStringLiteral(":/icons/chevron_down_dark.png");
    const QString kpiTitleColor = isLight ? c.headerText : c.textSecondary;
    const QString btnSubtleBg = isLight ? QStringLiteral("#F3F7FC") : QStringLiteral("#1A2640");
    const QString btnSubtleBorder = isLight ? c.border : QStringLiteral("#2A3B5C");
    const QString btnSubtleHover = isLight ? QStringLiteral("#E5EEF9") : QStringLiteral("#243456");
    const QString btnSubtlePressed = isLight ? QStringLiteral("#D5E2F4") : QStringLiteral("#2D4069");
    const QString primaryHover = isLight ? QStringLiteral("#46659E") : QStringLiteral("#4C6FAE");
    const QString dangerSolidBg = isLight ? QStringLiteral("#C94D43") : QStringLiteral("#C24B42");
    const QString dangerSolidHover = isLight ? QStringLiteral("#B34037") : QStringLiteral("#A93E36");
    const QString salmonBg = isLight ? QStringLiteral("#FDEEEA") : QStringLiteral("#331D22");
    const QString salmonFg = isLight ? QStringLiteral("#B03E34") : QStringLiteral("#F69B91");
    const QString salmonBorder = isLight ? c.dangerBorder : QStringLiteral("#7A3C43");
    const QString salmonHoverBg = isLight ? QStringLiteral("#FBDCD5") : QStringLiteral("#43242B");
    const QString rowHoverBg = isLight ? QStringLiteral("#EDF3FB") : QStringLiteral("#1A2742");
    const QString scrollHandle = isLight ? QStringLiteral("#C5D3E6") : QStringLiteral("#2B3D60");

    QString qss = QStringLiteral(
        "QMainWindow, QDialog { background-color: @PAGE_BG@; color: @TEXT_PRIMARY@; }"
        "QWidget { color: @TEXT_PRIMARY@; font-family: 'Inter', 'Noto Sans', 'Segoe UI', sans-serif; font-size: 13px; }"
        "QWidget#rowActionCell { background-color: transparent; border: none; }"
        "QFrame#headerCard, QFrame#kpiCard, QFrame#inspectorCard, QFrame#toolbarCard { "
        "  background-color: @CARD_BG@; border: 1px solid @BORDER@; border-radius: 10px; "
        "}"
        "QFrame#warningBanner { background-color: @WARN_BG@; border: 1px solid @WARN_BORDER@; border-radius: 8px; }"
        "QLabel#appTitle { font-size: 18px; font-weight: 700; color: @TEXT_PRIMARY@; }"
        "QLabel#appSubtitle { font-size: 12px; color: @TEXT_SECONDARY@; }"
        "QLabel#kpiTitle { font-size: 11px; font-weight: 700; color: @KPI_TITLE@; letter-spacing: 0.4px; }"
        "QLabel#kpiValue { font-size: 15px; font-weight: 700; color: @TEXT_PRIMARY@; }"
        "QLabel#kpiSub { font-size: 11px; color: @TEXT_SECONDARY@; }"
        "QLabel#kpiSub[state='warn'] { color: @WARN_FG@; font-weight: 600; }"
        "QLabel#mutedLabel { color: @TEXT_SECONDARY@; font-size: 12px; }"
        "QLabel#warnLabel { color: @WARN_FG@; font-size: 12px; font-weight: 600; }"
        "QPushButton { "
        "  background-color: @BTN_BG@; color: @TEXT_PRIMARY@; border: 1px solid @BTN_BORDER@; "
        "  border-radius: 6px; padding: 6px 12px; font-weight: 600; "
        "}"
        "QPushButton:hover { background-color: @BTN_HOVER@; border-color: @SLATE_ACCENT@; }"
        "QPushButton:pressed { background-color: @BTN_PRESSED@; }"
        "QPushButton:disabled { background-color: @INNER_BG@; color: @TEXT_SECONDARY@; border-color: @SUBTLE_BORDER@; }"
        "QPushButton[variant='primary'] { background-color: @SLATE_BLUE@; color: #FFFFFF; border: 1px solid @SLATE_BLUE@; }"
        "QPushButton[variant='primary']:hover { background-color: @PRIMARY_HOVER@; border-color: @PRIMARY_HOVER@; }"
        "QPushButton[variant='danger'] { background-color: @DANGER_SOLID@; color: #FFFFFF; border: 1px solid @DANGER_SOLID_HOVER@; }"
        "QPushButton[variant='danger']:hover { background-color: @DANGER_SOLID_HOVER@; border-color: @DANGER_SOLID_HOVER@; }"
        "QPushButton[variant='salmon'] { background-color: @SALMON_BG@; color: @SALMON_FG@; border: 1px solid @SALMON_BORDER@; font-weight: 600; }"
        "QPushButton[variant='salmon']:hover { background-color: @SALMON_HOVER@; border-color: @SALMON@; }"
        "QPushButton[variant='pill'] { background-color: @BTN_BG@; color: @TEXT_SECONDARY@; border: 1px solid @BTN_BORDER@; border-radius: 12px; padding: 4px 12px; font-size: 12px; min-height: 18px; }"
        "QPushButton[variant='pill']:hover { background-color: @BTN_HOVER@; color: @TEXT_PRIMARY@; border-color: @SLATE_ACCENT@; }"
        "QPushButton[variant='pill']:checked { background-color: @SLATE_BLUE@; color: #FFFFFF; border-color: @SLATE_BLUE@; }"
        "QPushButton[variant='pill']:checked:hover { background-color: @PRIMARY_HOVER@; color: #FFFFFF; border-color: @PRIMARY_HOVER@; }"
        "QPushButton[compact='true'] { padding: 2px 10px; font-size: 12px; }"
        "QPushButton[compact='icon'] { padding: 0px; font-size: 13px; }"
        "QPushButton[role='deleteIcon']:hover { background-color: @DANGER_BG@; color: @DANGER_FG@; border-color: @DANGER_BORDER@; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { "
        "  background-color: @INNER_BG@; color: @TEXT_PRIMARY@; border: 1px solid @BORDER@; border-radius: 6px; padding: 5px 8px; "
        "}"
        "QLineEdit:hover, QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: @SLATE_ACCENT@; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: @SLATE_BLUE@; background-color: @CARD_BG@; }"
        "QComboBox { padding-right: 26px; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 22px; border: none; border-left: 1px solid @SUBTLE_BORDER@; background: transparent; }"
        "QComboBox::down-arrow { image: url(@CHEVRON_ICON@); width: 12px; height: 12px; }"
        "QComboBox QAbstractItemView { background-color: @CARD_BG@; color: @TEXT_PRIMARY@; border: 1px solid @BORDER@; selection-background-color: @SELECT_BG@; selection-color: @TEXT_PRIMARY@; outline: none; }"
        "QCheckBox { spacing: 6px; color: @TEXT_PRIMARY@; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid @BORDER@; background-color: @INNER_BG@; }"
        "QCheckBox::indicator:hover { border-color: @SLATE_ACCENT@; }"
        "QCheckBox::indicator:checked { background-color: @SLATE_BLUE@; border-color: @SLATE_BLUE@; image: url(:/icons/check_white.png); }"
        "QTableWidget { "
        "  background-color: @CARD_BG@; alternate-background-color: @TABLE_ALT@; color: @TEXT_PRIMARY@; "
        "  border: 1px solid @BORDER@; border-radius: 8px; gridline-color: @SUBTLE_BORDER@; selection-background-color: @SELECT_BG@; selection-color: @TEXT_PRIMARY@; "
        "}"
        "QTableWidget::item { padding: 4px 8px; }"
        "QTableWidget::item:hover { background-color: @ROW_HOVER@; }"
        "QTableWidget::item:selected { background-color: @SELECT_BG@; color: @TEXT_PRIMARY@; }"
        "QHeaderView { background-color: @HEADER_BG@; border: none; }"
        "QHeaderView::section { "
        "  background-color: @HEADER_BG@; color: @HEADER_TEXT@; border: none; border-bottom: 1px solid @BORDER@; "
        "  border-right: 1px solid @SUBTLE_BORDER@; padding: 7px 8px; font-weight: 700; font-size: 12px; "
        "}"
        "QTableCornerButton::section { background-color: @HEADER_BG@; border: none; border-bottom: 1px solid @BORDER@; border-right: 1px solid @SUBTLE_BORDER@; }"
        "QPlainTextEdit#logViewer { "
        "  background-color: @LOG_BG@; color: @LOG_FG@; border: 1px solid @BORDER@; border-radius: 8px; "
        "  font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace; font-size: 12px; padding: 8px; "
        "  selection-background-color: @SELECT_BG@; selection-color: @TEXT_PRIMARY@; "
        "}"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: @SCROLL_HANDLE@; min-height: 28px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: @SLATE_ACCENT@; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical, QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; height: 0px; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: @SCROLL_HANDLE@; min-width: 28px; border-radius: 4px; }"
        "QScrollBar::handle:horizontal:hover { background: @SLATE_ACCENT@; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal, QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; width: 0px; }"
        "QMenu { background-color: @CARD_BG@; color: @TEXT_PRIMARY@; border: 1px solid @BORDER@; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 6px 14px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: @SLATE_BLUE@; color: #FFFFFF; }"
        "QToolTip { background-color: @CARD_BG@; color: @TEXT_PRIMARY@; border: 1px solid @SLATE_ACCENT@; border-radius: 4px; padding: 5px 8px; }"
        "QSplitter::handle { background-color: @SUBTLE_BORDER@; height: 4px; border-radius: 2px; }"
    );

    const QPair<QString, QString> tokens[] = {
        {QStringLiteral("@PAGE_BG@"), c.pageBg},
        {QStringLiteral("@CARD_BG@"), c.cardBg},
        {QStringLiteral("@INNER_BG@"), c.innerBg},
        {QStringLiteral("@BORDER@"), c.border},
        {QStringLiteral("@SUBTLE_BORDER@"), c.subtleBorder},
        {QStringLiteral("@TEXT_PRIMARY@"), c.textPrimary},
        {QStringLiteral("@TEXT_SECONDARY@"), c.textSecondary},
        {QStringLiteral("@CHEVRON_ICON@"), chevronIcon},
        {QStringLiteral("@KPI_TITLE@"), kpiTitleColor},
        {QStringLiteral("@WARN_BG@"), c.warnBg},
        {QStringLiteral("@WARN_BORDER@"), c.warnBorder},
        {QStringLiteral("@WARN_FG@"), c.warnFg},
        {QStringLiteral("@DANGER_BG@"), c.dangerBg},
        {QStringLiteral("@DANGER_BORDER@"), c.dangerBorder},
        {QStringLiteral("@DANGER_FG@"), c.dangerFg},
        {QStringLiteral("@BTN_BG@"), btnSubtleBg},
        {QStringLiteral("@BTN_BORDER@"), btnSubtleBorder},
        {QStringLiteral("@BTN_HOVER@"), btnSubtleHover},
        {QStringLiteral("@BTN_PRESSED@"), btnSubtlePressed},
        {QStringLiteral("@SLATE_BLUE@"), c.slateBlue},
        {QStringLiteral("@SLATE_ACCENT@"), c.slateAccent},
        {QStringLiteral("@PRIMARY_HOVER@"), primaryHover},
        {QStringLiteral("@DANGER_SOLID@"), dangerSolidBg},
        {QStringLiteral("@DANGER_SOLID_HOVER@"), dangerSolidHover},
        {QStringLiteral("@SALMON_BG@"), salmonBg},
        {QStringLiteral("@SALMON_FG@"), salmonFg},
        {QStringLiteral("@SALMON_BORDER@"), salmonBorder},
        {QStringLiteral("@SALMON_HOVER@"), salmonHoverBg},
        {QStringLiteral("@SALMON@"), c.salmon},
        {QStringLiteral("@TABLE_ALT@"), c.tableAltBg},
        {QStringLiteral("@ROW_HOVER@"), rowHoverBg},
        {QStringLiteral("@SELECT_BG@"), c.selectBg},
        {QStringLiteral("@HEADER_BG@"), c.headerBg},
        {QStringLiteral("@HEADER_TEXT@"), c.headerText},
        {QStringLiteral("@LOG_BG@"), c.logBg},
        {QStringLiteral("@LOG_FG@"), c.logFg},
        {QStringLiteral("@SCROLL_HANDLE@"), scrollHandle},
    };
    for (const auto& kv : tokens) {
        qss.replace(kv.first, kv.second);
    }
    return qss;
}

QIcon appIcon()
{
    QIcon icon;
    static constexpr int kSizes[] = {16, 24, 32, 48, 64, 128, 256, 512};
    for (int sz : kSizes) {
        icon.addFile(QStringLiteral(":/icons/kristatech-miner-manager_%1.png").arg(sz), QSize(sz, sz));
    }
    if (icon.availableSizes().isEmpty()) {
        icon.addFile(QStringLiteral(":/logo.png"));
    }
    return icon;
}

} // namespace Theme
