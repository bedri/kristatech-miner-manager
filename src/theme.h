#pragma once

#include <QIcon>
#include <QPalette>
#include <QString>

class QApplication;

/**
 * KristaTech Light/Dark visual theme colors and Qt stylesheet generator.
 */
struct ThemeColors {
    QString pageBg;
    QString cardBg;
    QString innerBg;
    QString border;
    QString textPrimary;
    QString textSecondary;
    QString headerBg;
    QString headerText;
    QString tableAltBg;
    QString selectBg;
    QString slateBlue;
    QString slateAccent;
    QString salmon;
    QString successBg;
    QString successFg;
    QString warnBg;
    QString warnFg;
    QString dangerBg;
    QString dangerFg;
    QString subtleBorder;
    QString warnBorder;
    QString dangerBorder;
    QString logBg;
    QString logFg;
};

namespace Theme {

ThemeColors colors(bool isLight);
QPalette buildPalette(bool isLight);
QString buildStylesheet(bool isLight);
void applyTheme(QApplication* app, bool isLight);
QIcon appIcon();

} // namespace Theme
