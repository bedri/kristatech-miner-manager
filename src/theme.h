#pragma once

#include <QString>

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
};

namespace Theme {

ThemeColors colors(bool isLight);
QString buildStylesheet(bool isLight);

} // namespace Theme
