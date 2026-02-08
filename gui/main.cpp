#include "MainWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("prober");
    app.setOrganizationName("prober");

    gui::MainWindow window;
    window.show();

    return app.exec();
}
