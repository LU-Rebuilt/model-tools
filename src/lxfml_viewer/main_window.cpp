#include "main_window.h"

#include "zip_extract.h"
#include "lego/brick_assembly/brick_assembly.h"
#include "lego/brick_geometry/brick_geometry.h"
#include "lego/brick_assembly/brick_colors.h"
#include "havok/writer/hkx_writer.h"
#include "havok/types/hkx_types.h"

#include <cmath>
#include <limits>

#include <QMenuBar>
#include <QDockWidget>
#include <QStatusBar>
#include "file_browser.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>

namespace lxfml_viewer {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("LXFML Viewer");
    resize(1024, 768);

    gl_widget_ = new LxfmlGLWidget(this);
    setCentralWidget(gl_widget_);

    // Menu bar
    auto* file_menu = menuBar()->addMenu("&File");

    file_menu->addAction("&Open...", QKeySequence::Open,
                         this, &MainWindow::onFileOpen);

    file_menu->addSeparator();

    file_menu->addAction("&Generate HKX", QKeySequence("Ctrl+G"),
                         this, &MainWindow::onGenerateHkx);

    file_menu->addAction("&Save HKX...", QKeySequence("Ctrl+Shift+S"),
                         this, &MainWindow::onSaveHkx);

    file_menu->addSeparator();

    file_menu->addAction("Set &Client Root...", QKeySequence("Ctrl+R"),
                         this, [this]() {
        QSettings settings;
        QString current = clientRoot_.isEmpty()
            ? settings.value("lxfml_client_root").toString()
            : clientRoot_;
        QString dir = QFileDialog::getExistingDirectory(
            this, "Select Client Root (res/ or parent of res/)", current,
            QFileDialog::DontUseNativeDialog);
        if (!dir.isEmpty()) {
            clientRoot_ = dir;
            settings.setValue("lxfml_client_root", dir);
            rebuildBrickDB();
            statusBar()->showMessage(
                QString("Client root: %1 (%2 brick geometries indexed)")
                    .arg(dir).arg(brickDB_.entryCount()), 5000);
        }
    });

    file_menu->addSeparator();

    file_menu->addAction("&Quit", QKeySequence::Quit,
                         this, &QMainWindow::close);

    // View menu
    auto* view_menu = menuBar()->addMenu("&View");

    auto* showBricksAct = view_menu->addAction("Show &Bricks");
    showBricksAct->setCheckable(true);
    showBricksAct->setChecked(true);
    connect(showBricksAct, &QAction::toggled, [this](bool c) {
        gl_widget_->showBricks = c; gl_widget_->update(); });

    auto* showHkxAct = view_menu->addAction("Show &HKX Collision");
    showHkxAct->setCheckable(true);
    showHkxAct->setChecked(true);
    connect(showHkxAct, &QAction::toggled, [this](bool c) {
        gl_widget_->showHkx = c; gl_widget_->update(); });

    view_menu->addSeparator();

    auto* showWireAct = view_menu->addAction("Show &Wireframe");
    showWireAct->setCheckable(true);
    showWireAct->setChecked(true);
    connect(showWireAct, &QAction::toggled, [this](bool c) {
        gl_widget_->showWireframe = c; gl_widget_->update(); });

    // Tree dock
    auto* treeDock = new QDockWidget("Model Structure", this);
    treeDock->setObjectName("LxfmlStructure");
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Name", "Details"});
    tree_->setColumnWidth(0, 220);
    tree_->setAlternatingRowColors(true);
    treeDock->setWidget(tree_);
    addDockWidget(Qt::RightDockWidgetArea, treeDock);

    connect(tree_, &QTreeWidget::currentItemChanged,
            this, [this]() { onTreeItemSelected(); });

    // Status bar
    status_label_ = new QLabel("No file loaded");
    statusBar()->addPermanentWidget(status_label_);

    connect(gl_widget_, &LxfmlGLWidget::modelLoaded,
            this, &MainWindow::onModelLoaded);

    // Restore client root
    QSettings settings;
    clientRoot_ = settings.value("lxfml_client_root").toString();
    if (!clientRoot_.isEmpty()) {
        rebuildBrickDB();
    }
}

std::vector<uint8_t> MainWindow::loadFileData(const QString& path) {
    if (path.endsWith(".lxf", Qt::CaseInsensitive)) {
        // LXF is a ZIP archive; extract IMAGE100.LXFML
        return zip_extract_file(path.toStdString(), "IMAGE100.LXFML");
    } else {
        // Plain LXFML XML file
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("Could not open file");
        }
        QByteArray raw = file.readAll();
        return std::vector<uint8_t>(raw.begin(), raw.end());
    }
}

void MainWindow::detectClientRoot(const QString& filePath) {
    if (!clientRoot_.isEmpty() && QDir(clientRoot_).exists())
        return;

    // Walk up from the file looking for a res/ directory with brickdb.zip
    // or brickprimitives/
    QFileInfo fi(filePath);
    QDir dir = fi.absoluteDir();

    for (int i = 0; i < 10; ++i) {
        // Check if this IS the res directory
        if (QFile::exists(dir.absoluteFilePath("brickdb.zip")) ||
            QDir(dir.absoluteFilePath("brickprimitives")).exists()) {
            clientRoot_ = dir.absolutePath();
            QSettings settings;
            settings.setValue("lxfml_client_root", clientRoot_);
            rebuildBrickDB();
            return;
        }
        // Check for res/ subdirectory
        QDir resDir(dir.absoluteFilePath("res"));
        if (resDir.exists() &&
            (QFile::exists(resDir.absoluteFilePath("brickdb.zip")) ||
             QDir(resDir.absoluteFilePath("brickprimitives")).exists())) {
            clientRoot_ = resDir.absolutePath();
            QSettings settings;
            settings.setValue("lxfml_client_root", clientRoot_);
            rebuildBrickDB();
            return;
        }
        if (!dir.cdUp()) break;
    }
}

void MainWindow::rebuildBrickDB() {
    brickDB_ = BrickDB();
    if (!clientRoot_.isEmpty()) {
        brickDB_.loadFromDirectory(clientRoot_.toStdString());
    }
}

bool MainWindow::openFile(const QString& path) {
    // Load LXFML data (handles .lxf ZIP extraction)
    std::vector<uint8_t> data;
    try {
        data = loadFileData(path);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error",
            QString("Could not load file:\n%1\n%2").arg(path, e.what()));
        return false;
    }

    // Parse LXFML
    try {
        currentLxfml_ = lu::assets::lxfml_parse(
            std::span<const uint8_t>(data.data(), data.size()));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error",
            QString("Failed to parse LXFML:\n%1").arg(e.what()));
        return false;
    }

    // Auto-detect client root if not set
    detectClientRoot(path);

    if (brickDB_.empty()) {
        QMessageBox::information(this, "Client Root",
            "No brick geometry found. Use File > Set Client Root to point to "
            "the client's res/ directory (containing brickdb.zip or brickprimitives/).");
    }

    // Assemble the model using the brick database
    auto loader = [this](int designID, int partIndex) -> lu::assets::BrickGeometry {
        auto data = brickDB_.loadGeometry(designID, partIndex);
        if (data.empty()) return lu::assets::BrickGeometry{};
        return lu::assets::brick_geometry_parse(
            std::span<const uint8_t>(data.data(), data.size()));
    };

    currentAssembly_ = lu::assets::assemble_lxfml(currentLxfml_, loader);
    hasModel_ = true;
    currentFilePath_ = path;

    gl_widget_->loadAssembly(currentAssembly_);

    QFileInfo fi(path);
    setWindowTitle(QString("LXFML Viewer - %1").arg(fi.fileName()));

    buildTree();

    if (currentAssembly_.bricks_missing > 0) {
        statusBar()->showMessage(
            QString("%1/%2 bricks missing geometry (%3 indexed)")
                .arg(currentAssembly_.bricks_missing)
                .arg(currentAssembly_.bricks_missing + currentAssembly_.bricks_loaded)
                .arg(brickDB_.entryCount()),
            10000);
    }

    return true;
}

void MainWindow::onFileOpen() {
    QSettings settings;
    QString lastDir = settings.value("lxfml_last_open_dir").toString();

    QString path = qt_common::FileBrowserDialog::getOpenFileName(this,
        "Open LXFML/LXF File", lastDir,
        "LEGO Models (*.lxfml *.lxf);;LXFML Files (*.lxfml);;LXF Files (*.lxf);;All Files (*)");

    if (!path.isEmpty()) {
        settings.setValue("lxfml_last_open_dir",
            QFileInfo(path).absolutePath());
        openFile(path);
    }
}

void MainWindow::onModelLoaded(uint32_t bricks, uint32_t vertices, uint32_t triangles) {
    status_label_->setText(
        QString("Bricks: %1 | Vertices: %2 | Triangles: %3")
            .arg(bricks).arg(vertices).arg(triangles));
}

void MainWindow::buildTree() {
    buildingTree_ = true;
    tree_->clear();

    const auto& lxfml = currentLxfml_;

    // File info
    auto* fileItem = new QTreeWidgetItem(tree_);
    fileItem->setText(0, "LXFML File");
    fileItem->setText(1, QString("v%1.%2 | %3")
        .arg(lxfml.version_major).arg(lxfml.version_minor)
        .arg(QString::fromStdString(lxfml.name)));
    fileItem->setExpanded(true);

    // Meta
    if (!lxfml.meta.application.name.empty()) {
        auto* meta = new QTreeWidgetItem(fileItem);
        meta->setText(0, "Application");
        meta->setText(1, QString("%1 v%2.%3")
            .arg(QString::fromStdString(lxfml.meta.application.name))
            .arg(lxfml.meta.application.version_major)
            .arg(lxfml.meta.application.version_minor));
    }

    // Cameras
    if (!lxfml.cameras.empty()) {
        auto* camRoot = new QTreeWidgetItem(fileItem);
        camRoot->setText(0, QString("Cameras (%1)").arg(lxfml.cameras.size()));
        for (size_t i = 0; i < lxfml.cameras.size(); ++i) {
            const auto& cam = lxfml.cameras[i];
            auto* item = new QTreeWidgetItem(camRoot);
            item->setText(0, QString("Camera %1").arg(cam.ref_id));
            item->setText(1, QString("fov=%1 dist=%2")
                .arg(static_cast<double>(cam.field_of_view), 0, 'f', 1)
                .arg(static_cast<double>(cam.distance), 0, 'f', 1));
        }
    }

    // Bricks
    if (!lxfml.bricks.empty()) {
        auto* brickRoot = new QTreeWidgetItem(tree_);
        brickRoot->setText(0, QString("Bricks (%1)").arg(lxfml.bricks.size()));
        brickRoot->setExpanded(true);

        int meshIdx = 0;
        for (size_t i = 0; i < lxfml.bricks.size(); ++i) {
            const auto& brick = lxfml.bricks[i];
            auto* brickItem = new QTreeWidgetItem(brickRoot);
            brickItem->setText(0, QString("Brick %1").arg(brick.ref_id));

            QStringList details;
            details << QString("design %1").arg(brick.design_id);
            details << QString("%1 parts").arg(brick.parts.size());
            brickItem->setText(1, details.join(" | "));

            // Check if this brick has geometry (was loaded)
            bool hasGeom = false;
            if (meshIdx < static_cast<int>(currentAssembly_.bricks.size()) &&
                currentAssembly_.bricks[meshIdx].brick_ref_id == brick.ref_id) {
                brickItem->setData(0, Qt::UserRole + 1, meshIdx);
                hasGeom = true;
                meshIdx++;
            }

            for (size_t pi = 0; pi < brick.parts.size(); ++pi) {
                const auto& part = brick.parts[pi];
                auto* partItem = new QTreeWidgetItem(brickItem);
                partItem->setText(0, QString("Part %1").arg(part.ref_id));

                QStringList pdetails;
                pdetails << QString("design %1").arg(part.design_id);
                if (!part.materials.empty())
                    pdetails << QString("mat: %1").arg(QString::fromStdString(part.materials));
                pdetails << QString("%1 bones").arg(part.bones.size());
                partItem->setText(1, pdetails.join(" | "));

                if (hasGeom)
                    partItem->setData(0, Qt::UserRole + 1, meshIdx - 1);
            }
        }
    }

    // Rigid Systems
    if (!lxfml.rigid_systems.empty()) {
        auto* rsRoot = new QTreeWidgetItem(tree_);
        rsRoot->setText(0, QString("Rigid Systems (%1)").arg(lxfml.rigid_systems.size()));
        for (size_t i = 0; i < lxfml.rigid_systems.size(); ++i) {
            const auto& rs = lxfml.rigid_systems[i];
            auto* rsItem = new QTreeWidgetItem(rsRoot);
            rsItem->setText(0, QString("RigidSystem %1").arg(i));
            rsItem->setText(1, QString("%1 rigids, %2 joints")
                .arg(rs.rigids.size()).arg(rs.joints.size()));
        }
    }

    // Scene (v4 LDD)
    if (!lxfml.scene.models.empty()) {
        auto* sceneRoot = new QTreeWidgetItem(tree_);
        sceneRoot->setText(0, "Scene (v4 LDD)");
        for (const auto& model : lxfml.scene.models) {
            auto* modelItem = new QTreeWidgetItem(sceneRoot);
            modelItem->setText(0, QString::fromStdString(model.name));
            modelItem->setText(1, QString("%1 groups").arg(model.groups.size()));
            for (const auto& group : model.groups) {
                auto* groupItem = new QTreeWidgetItem(modelItem);
                groupItem->setText(0, group.name.empty()
                    ? QString("Group %1").arg(group.ref_id)
                    : QString::fromStdString(group.name));
                groupItem->setText(1, QString("%1 parts").arg(group.parts.size()));
            }
        }
    }

    tree_->expandToDepth(0);
    buildingTree_ = false;
}

void MainWindow::onTreeItemSelected() {
    if (buildingTree_) return;
    auto* item = tree_->currentItem();
    if (!item) return;

    QVariant meshData = item->data(0, Qt::UserRole + 1);
    if (meshData.isValid()) {
        gl_widget_->setSelectedIndex(meshData.toInt());
    } else {
        gl_widget_->setSelectedIndex(-1);
    }
    statusBar()->showMessage(item->text(0) + "  " + item->text(1), 5000);
}

void MainWindow::onGenerateHkx() {
    if (!hasModel_ || currentAssembly_.bricks.empty()) {
        QMessageBox::warning(this, "Generate HKX", "Load an LXFML file first.");
        return;
    }

    // Build a box collider for each brick from its vertex AABB
    std::vector<Hkx::ShapeInfo> boxShapes;

    for (const auto& brick : currentAssembly_.bricks) {
        if (brick.vertices.size() < 3) continue;

        // Compute AABB
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        float maxZ = std::numeric_limits<float>::lowest();

        for (size_t i = 0; i + 2 < brick.vertices.size(); i += 3) {
            float x = brick.vertices[i];
            float y = brick.vertices[i + 1];
            float z = brick.vertices[i + 2];
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
            minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
        }

        float halfX = (maxX - minX) * 0.5f;
        float halfY = (maxY - minY) * 0.5f;
        float halfZ = (maxZ - minZ) * 0.5f;
        float cenX = (minX + maxX) * 0.5f;
        float cenY = (minY + maxY) * 0.5f;
        float cenZ = (minZ + maxZ) * 0.5f;

        // Skip degenerate boxes
        if (halfX < 1e-6f && halfY < 1e-6f && halfZ < 1e-6f) continue;

        // Create box shape
        Hkx::ShapeInfo box;
        box.type = Hkx::ShapeType::Box;
        box.className = "hkpBoxShape";
        box.halfExtents = {halfX, halfY, halfZ, 0.0f};
        box.radius = 0.05f;

        // Wrap in ConvexTranslateShape to position at AABB center
        Hkx::ShapeInfo translated;
        translated.type = Hkx::ShapeType::ConvexTranslate;
        translated.className = "hkpConvexTranslateShape";
        translated.translation = {cenX, cenY, cenZ, 0.0f};
        translated.radius = box.radius;
        translated.children.push_back(std::move(box));

        boxShapes.push_back(std::move(translated));
    }

    if (boxShapes.empty()) {
        QMessageBox::warning(this, "Generate HKX", "No brick geometry to convert.");
        return;
    }

    // Build the final shape — ListShape if multiple, single shape if one
    Hkx::ShapeInfo rootShape;
    if (boxShapes.size() == 1) {
        rootShape = std::move(boxShapes[0]);
    } else {
        rootShape.type = Hkx::ShapeType::List;
        rootShape.className = "hkpListShape";
        rootShape.children = std::move(boxShapes);
    }

    // Build rigid body (static)
    Hkx::RigidBodyInfo rb;
    rb.shape = std::move(rootShape);
    rb.material.friction = 0.5f;
    rb.material.restitution = 0.4f;
    rb.material.responseType = 0;
    rb.mass = 0.0f;

    // Build physics hierarchy
    Hkx::PhysicsSystemInfo sys;
    sys.rigidBodies.push_back(std::move(rb));

    Hkx::PhysicsDataInfo pd;
    pd.systems.push_back(std::move(sys));

    currentHkx_ = Hkx::ParseResult{};
    currentHkx_.success = true;
    currentHkx_.havokVersion = "Havok-7.1.0-r1";
    currentHkx_.fileVersion = 7;
    currentHkx_.pointerSize = 4;
    currentHkx_.physicsData.push_back(std::move(pd));

    Hkx::RootLevelContainerInfo rlc;
    rlc.namedVariants.push_back({"Physics Data", "hkpPhysicsData", 0});
    currentHkx_.rootContainers.push_back(std::move(rlc));
    hasHkx_ = true;

    // Display in viewport as overlay
    gl_widget_->loadHkxOverlay(currentHkx_);

    int boxCount = static_cast<int>(currentAssembly_.bricks.size() - currentAssembly_.bricks_missing);
    statusBar()->showMessage(
        QString("HKX generated: %1 box colliders — toggle View > Show HKX/Bricks to compare")
            .arg(boxCount), 10000);
}

void MainWindow::onSaveHkx() {
    if (!hasHkx_) {
        QMessageBox::warning(this, "Save HKX", "Generate HKX first (Ctrl+G).");
        return;
    }

    QFileInfo fi(currentFilePath_);
    QString defaultName = fi.absolutePath() + "/" + fi.completeBaseName() + ".hkx";

    QString path = QFileDialog::getSaveFileName(this,
        "Save HKX File", defaultName, "HKX Files (*.hkx);;All Files (*)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".hkx", Qt::CaseInsensitive))
        path += ".hkx";

    Hkx::HkxWriter writer;
    if (writer.Write(path.toStdString(), currentHkx_)) {
        statusBar()->showMessage(QString("Saved: %1").arg(path), 5000);
    } else {
        QMessageBox::warning(this, "Save HKX",
            QString("Failed to write HKX:\n%1")
                .arg(QString::fromStdString(writer.GetError())));
    }
}

} // namespace lxfml_viewer
