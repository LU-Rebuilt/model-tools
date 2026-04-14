#pragma once

#include "lxfml_gl_widget.h"
#include "brick_db.h"
#include "lego/lxfml/lxfml_reader.h"

#include <QMainWindow>
#include <QLabel>
#include <QTreeWidget>

namespace lxfml_viewer {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openFile(const QString& path);

private slots:
    void onFileOpen();
    void onModelLoaded(uint32_t bricks, uint32_t vertices, uint32_t triangles);
    void onTreeItemSelected();

private:
    // Parse LXFML data (handles both raw .lxfml and .lxf ZIP extraction).
    std::vector<uint8_t> loadFileData(const QString& path);

    // Build the tree from the parsed LXFML and assembly.
    void buildTree();

    // Try to auto-detect client root from the file path.
    void detectClientRoot(const QString& filePath);

    // Rebuild the brick database from the current client root.
    void rebuildBrickDB();

    LxfmlGLWidget* gl_widget_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTreeWidget* tree_ = nullptr;

    lu::assets::LxfmlFile currentLxfml_;
    lu::assets::AssemblyResult currentAssembly_;
    bool hasModel_ = false;

    // Client root (res/ or parent of res/)
    QString clientRoot_;
    BrickDB brickDB_;

    bool buildingTree_ = false;
};

} // namespace lxfml_viewer
