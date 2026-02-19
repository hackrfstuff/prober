#pragma once

#include <QString>
#include <QApplication>
#include <QPalette>
#include <QStyleHints>

namespace gui {

inline bool isDarkMode() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto scheme = QApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) return true;
    if (scheme == Qt::ColorScheme::Light) return false;
#endif
    QPalette pal = QApplication::palette();
    return pal.color(QPalette::Window).lightness() < 128;
}

struct Theme {
    QString windowBg;
    QString panelBg;
    QString topBarBg;
    QString topBarBorder;
    QString inputBg;

    QString textPrimary;
    QString textSecondary;
    QString textMuted;

    QString border;
    QString borderLight;

    QString btnPrimaryBg;
    QString btnPrimaryHoverBg;
    QString btnPrimaryFg;
    QString btnSecondaryBorder;
    QString btnSecondaryHoverBg;

    QString listBg;
    QString listSelectedBg;
    QString listHoverBg;
    QString listItemText;

    QString logBg;
    QString logFg;

    QString warningBg;
    QString warningFg;

    QString tabPaneBg;
    QString tabPaneBorder;
    QString tabSelectedBg;
    QString tabSelectedIndicator;

    QString badgeOkBg;      QString badgeOkFg;
    QString badgeWarnBg;    QString badgeWarnFg;
    QString badgeFailBg;    QString badgeFailFg;
    QString badgeBusyBg;    QString badgeBusyFg;
    QString badgeDefaultBg; QString badgeDefaultFg;

    QString successFg;
    QString errorFg;
    QString dirtyIndicator;
    QString mutedStatusFg;
    QString linkColor;

    QString dotNeutral;
    QString dotOk;
    QString dotFail;

    QString btnGreenBg;
    QString btnGreenHoverBg;
    QString btnRedBg;
    QString btnRedHoverBg;
};

inline Theme currentTheme() {
    Theme t;
    if (isDarkMode()) {
        t.windowBg          = "#1e1e1e";
        t.panelBg           = "#2d2d2d";
        t.topBarBg          = "#252525";
        t.topBarBorder       = "#3c3c3c";
        t.inputBg           = "#353535";

        t.textPrimary       = "#e0e0e0";
        t.textSecondary     = "#b0b0b0";
        t.textMuted         = "#808080";

        t.border            = "#3c3c3c";
        t.borderLight       = "#4a4a4a";

        t.btnPrimaryBg      = "#4a9eff";
        t.btnPrimaryHoverBg = "#5aadff";
        t.btnPrimaryFg      = "#ffffff";

        t.btnSecondaryBorder = "#4a4a4a";
        t.btnSecondaryHoverBg = "#353535";

        t.listBg            = "#2d2d2d";
        t.listSelectedBg    = "#3a4a5c";
        t.listHoverBg       = "#353535";
        t.listItemText      = "#e0e0e0";

        t.logBg             = "#1a1a1a";
        t.logFg             = "#d4d4d4";

        t.warningBg         = "#4a3a1a";
        t.warningFg         = "#f5c842";

        t.tabPaneBg         = "#2d2d2d";
        t.tabPaneBorder     = "#3c3c3c";
        t.tabSelectedBg     = "#2d2d2d";
        t.tabSelectedIndicator = "#4a9eff";

        t.badgeOkBg         = "#1a3a2a"; t.badgeOkFg     = "#4ade80";
        t.badgeWarnBg       = "#3a3020"; t.badgeWarnFg   = "#fbbf24";
        t.badgeFailBg       = "#3a1a1a"; t.badgeFailFg   = "#f87171";
        t.badgeBusyBg       = "#1a2a3a"; t.badgeBusyFg   = "#60a5fa";
        t.badgeDefaultBg    = "#353535"; t.badgeDefaultFg = "#b0b0b0";

        t.successFg         = "#4ade80";
        t.errorFg           = "#f87171";
        t.dirtyIndicator    = "#fbbf24";
        t.mutedStatusFg     = "#808080";
        t.linkColor         = "#f87171";

        t.dotNeutral        = "#6b6b6b";
        t.dotOk             = "#4ade80";
        t.dotFail           = "#f87171";

        t.btnGreenBg        = "#16a34a";
        t.btnGreenHoverBg   = "#15803d";
        t.btnRedBg          = "#dc2626";
        t.btnRedHoverBg     = "#b91c1c";
    } else {
        t.windowBg          = "#fafafa";
        t.panelBg           = "#ffffff";
        t.topBarBg          = "#f4f4f5";
        t.topBarBorder       = "#e4e4e7";
        t.inputBg           = "#ffffff";

        t.textPrimary       = "#18181b";
        t.textSecondary     = "#3f3f46";
        t.textMuted         = "#71717a";

        t.border            = "#e4e4e7";
        t.borderLight       = "#f4f4f5";

        t.btnPrimaryBg      = "#18181b";
        t.btnPrimaryHoverBg = "#27272a";
        t.btnPrimaryFg      = "#ffffff";

        t.btnSecondaryBorder = "#d4d4d8";
        t.btnSecondaryHoverBg = "#f4f4f5";

        t.listBg            = "#ffffff";
        t.listSelectedBg    = "#e0e7ff";
        t.listHoverBg       = "#f5f5f5";
        t.listItemText      = "#000000";

        t.logBg             = "#18181b";
        t.logFg             = "#fafafa";

        t.warningBg         = "#fef3c7";
        t.warningFg         = "#92400e";

        t.tabPaneBg         = "#ffffff";
        t.tabPaneBorder     = "#e4e4e7";
        t.tabSelectedBg     = "#ffffff";
        t.tabSelectedIndicator = "#18181b";

        t.badgeOkBg         = "#dcfce7"; t.badgeOkFg     = "#166534";
        t.badgeWarnBg       = "#fef3c7"; t.badgeWarnFg   = "#92400e";
        t.badgeFailBg       = "#fee2e2"; t.badgeFailFg   = "#991b1b";
        t.badgeBusyBg       = "#dbeafe"; t.badgeBusyFg   = "#1e40af";
        t.badgeDefaultBg    = "#f3f4f6"; t.badgeDefaultFg = "#374151";

        t.successFg         = "#16a34a";
        t.errorFg           = "#dc2626";
        t.dirtyIndicator    = "#f59e0b";
        t.mutedStatusFg     = "#71717a";
        t.linkColor         = "#dc2626";

        t.dotNeutral        = "#9ca3af";
        t.dotOk             = "#22c55e";
        t.dotFail           = "#ef4444";

        t.btnGreenBg        = "#16a34a";
        t.btnGreenHoverBg   = "#15803d";
        t.btnRedBg          = "#dc2626";
        t.btnRedHoverBg     = "#b91c1c";
    }
    return t;
}

inline QString primaryButtonStyle(const Theme& t) {
    return QString(
        "QPushButton { background-color: %1; color: %2; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: %3; }"
        "QPushButton:disabled { opacity: 0.5; }")
        .arg(t.btnPrimaryBg, t.btnPrimaryFg, t.btnPrimaryHoverBg);
}

inline QString secondaryButtonStyle(const Theme& t) {
    return QString(
        "QPushButton { padding: 8px 16px; border: 1px solid %1; border-radius: 4px; color: %2; }"
        "QPushButton:hover { background-color: %3; }")
        .arg(t.btnSecondaryBorder, t.textPrimary, t.btnSecondaryHoverBg);
}

inline QString greenButtonStyle(const Theme& t) {
    return QString(
        "QPushButton { background-color: %1; color: white; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:disabled { opacity: 0.5; }")
        .arg(t.btnGreenBg, t.btnGreenHoverBg);
}

inline QString redButtonStyle(const Theme& t) {
    return QString(
        "QPushButton { background-color: %1; color: white; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:disabled { opacity: 0.5; }")
        .arg(t.btnRedBg, t.btnRedHoverBg);
}

inline QString globalStyleSheet(const Theme& t) {
    return QString(
        "QMainWindow { background-color: %1; color: %2; }"
        "QGroupBox { font-weight: bold; border: 1px solid %3; border-radius: 4px;"
        "  margin-top: 8px; padding-top: 8px; color: %2; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %2; }"
        "QLabel { color: %2; }"
        "QCheckBox { color: %2; }"
        "QComboBox { background-color: %4; color: %2; border: 1px solid %3; border-radius: 3px; padding: 2px 6px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: %4; color: %2; selection-background-color: %5; }"
        "QSpinBox { background-color: %4; color: %2; border: 1px solid %3; border-radius: 3px; padding: 2px 6px; }"
        "QLineEdit { background-color: %4; color: %2; border: 1px solid %3; border-radius: 3px; padding: 2px 6px; }"
        "QSlider::groove:horizontal { background: %3; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: %6; width: 14px; margin: -5px 0; border-radius: 7px; }"
        "QProgressBar { background-color: %4; border: 1px solid %3; border-radius: 3px; text-align: center; color: %2; }"
        "QProgressBar::chunk { background-color: %6; border-radius: 2px; }"
        "QScrollBar:vertical { background: %1; width: 8px; }"
        "QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(t.windowBg, t.textPrimary, t.border, t.inputBg, t.listSelectedBg, t.btnPrimaryBg);
}

} // namespace gui
