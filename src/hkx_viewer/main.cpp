// HKX Collision Viewer
// Graphical tool for viewing Havok physics collision shapes from .hkx files.
//
// Renders rigid body shapes (box, sphere, capsule, convex hull, triangle mesh,
// compressed mesh) as wireframe + transparent solid using legacy OpenGL.
//
// Usage:
//   hkx_viewer [file.hkx]
//
// If a path is given, loads it immediately. Otherwise use File > Open.

#include "main_window.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    // Request an OpenGL compatibility profile for legacy fixed-function pipeline
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    fmt.setVersion(2, 1);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setOrganizationName("LU-Rebuilt");
    app.setApplicationName("HkxViewer");

    hkx_viewer::MainWindow window;
    window.show();

    if (argc > 1) {
        window.loadFile(argv[1]);
    }

    return app.exec();
}
