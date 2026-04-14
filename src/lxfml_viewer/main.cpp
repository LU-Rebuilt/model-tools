#include "main_window.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("LU-Rebuilt");
    app.setApplicationName("lxfml_viewer");

    lxfml_viewer::MainWindow window;
    window.show();

    // Open file from command line argument
    if (argc > 1) {
        window.openFile(QString::fromUtf8(argv[1]));
    }

    return app.exec();
}
