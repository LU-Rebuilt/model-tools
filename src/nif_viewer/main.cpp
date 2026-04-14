#include "main_window.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[])
{
    // Request an OpenGL compatibility profile for legacy fixed-function GL.
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    fmt.setVersion(2, 1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setOrganizationName("LU-Rebuilt");
    app.setApplicationName("NifViewer");

    MainWindow window;
    window.show();

    // If a file path was passed as argument, open it immediately.
    if (argc > 1) {
        window.openFile(QString::fromLocal8Bit(argv[1]));
    }

    return app.exec();
}
