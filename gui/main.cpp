#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QStyleHints>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("prober");
    app.setOrganizationName("prober");

    app.setStyle(QStyleFactory::create("Fusion"));

    gui::Theme th = gui::currentTheme();

    if (gui::isDarkMode()) {
        QPalette pal;
        pal.setColor(QPalette::Window,          QColor(th.windowBg));
        pal.setColor(QPalette::WindowText,      QColor(th.textPrimary));
        pal.setColor(QPalette::Base,            QColor(th.inputBg));
        pal.setColor(QPalette::AlternateBase,   QColor(th.panelBg));
        pal.setColor(QPalette::Text,            QColor(th.textPrimary));
        pal.setColor(QPalette::Button,          QColor(th.panelBg));
        pal.setColor(QPalette::ButtonText,      QColor(th.textPrimary));
        pal.setColor(QPalette::BrightText,      QColor("#ffffff"));
        pal.setColor(QPalette::Highlight,       QColor(th.listSelectedBg));
        pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        pal.setColor(QPalette::ToolTipBase,     QColor(th.panelBg));
        pal.setColor(QPalette::ToolTipText,     QColor(th.textPrimary));
        pal.setColor(QPalette::PlaceholderText, QColor(th.textMuted));
        pal.setColor(QPalette::Link,            QColor(th.linkColor));
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(th.textMuted));
        pal.setColor(QPalette::Disabled, QPalette::Text,       QColor(th.textMuted));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(th.textMuted));
        app.setPalette(pal);
    }

    gui::MainWindow window;
    window.show();

    return app.exec();
}
