#include "main_window.h"

#include "havok/converters/nif_to_hkx.h"

#include <QMenuBar>
#include <QDockWidget>
#include <QStatusBar>
#include "file_browser.h"

#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <functional>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("NIF Viewer");
    resize(1024, 768);

    gl_widget_ = new nif_viewer::NifGLWidget(this);
    setCentralWidget(gl_widget_);

    // Menu bar
    auto* file_menu = menuBar()->addMenu("&File");

    auto* open_action = file_menu->addAction("&Open...");
    open_action->setShortcut(QKeySequence::Open);
    connect(open_action, &QAction::triggered, this, &MainWindow::onFileOpen);

    file_menu->addSeparator();

    file_menu->addAction("&Generate HKX", QKeySequence("Ctrl+G"),
                         this, &MainWindow::onGenerateHkx);

    file_menu->addAction("&Save HKX...", QKeySequence("Ctrl+Shift+S"),
                         this, &MainWindow::onSaveHkx);

    file_menu->addSeparator();

    auto* quit_action = file_menu->addAction("&Quit");
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &QMainWindow::close);

    // View menu
    auto* view_menu = menuBar()->addMenu("&View");

    auto* showNifAct = view_menu->addAction("Show &NIF Mesh");
    showNifAct->setCheckable(true);
    showNifAct->setChecked(true);
    connect(showNifAct, &QAction::toggled, [this](bool c) {
        gl_widget_->showNif = c; gl_widget_->update(); });

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
    auto* treeDock = new QDockWidget("NIF Structure", this);
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Name", "Details"});
    tree_->setColumnWidth(0, 200);
    tree_->setAlternatingRowColors(true);
    treeDock->setObjectName("NifStructure");
    treeDock->setWidget(tree_);
    addDockWidget(Qt::RightDockWidgetArea, treeDock);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this]() { onTreeItemSelected(); });

    // Status bar
    status_label_ = new QLabel("No file loaded");
    statusBar()->addPermanentWidget(status_label_);

    connect(gl_widget_, &nif_viewer::NifGLWidget::fileLoaded,
            this, &MainWindow::onFileLoaded);
}

bool MainWindow::openFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Error",
            QString("Could not open file:\n%1").arg(path));
        return false;
    }

    QByteArray raw = file.readAll();
    std::vector<uint8_t> data(raw.begin(), raw.end());

    // Parse NIF and keep it for HKX generation
    try {
        currentNif_ = lu::assets::nif_parse(
            std::span<const uint8_t>(data.data(), data.size()));
        hasNif_ = true;
    } catch (...) {
        hasNif_ = false;
    }

    if (!gl_widget_->loadNif(data)) {
        QMessageBox::warning(this, "Error",
            QString("Failed to parse NIF file:\n%1").arg(path));
        status_label_->setText("Parse error");
        hasNif_ = false;
        return false;
    }

    QFileInfo fi(path);
    setWindowTitle(QString("NIF Viewer - %1").arg(fi.fileName()));
    if (hasNif_) buildTree();
    return true;
}

void MainWindow::onFileOpen()
{
    QSettings settings;
    QString lastDir = settings.value("last_open_dir").toString();

    QString path = qt_common::FileBrowserDialog::getOpenFileName(this,
        "Open NIF File", lastDir,
        "NIF Files (*.nif *.kf *.kfm);;All Files (*)");

    if (!path.isEmpty()) {
        settings.setValue("last_open_dir",
            QFileInfo(path).absolutePath());
        openFile(path);
    }
}

void MainWindow::onFileLoaded(uint32_t blocks, uint32_t vertices, uint32_t triangles)
{
    status_label_->setText(
        QString("Blocks: %1 | Vertices: %2 | Triangles: %3")
            .arg(blocks).arg(vertices).arg(triangles));
}

void MainWindow::onGenerateHkx()
{
    if (!hasNif_) {
        QMessageBox::warning(this, "No NIF", "Load a NIF file first.");
        return;
    }

    auto result = Hkx::convertNifToHkx(currentNif_);
    if (!result.success) {
        QMessageBox::warning(this, "Error",
            QString("Failed to generate HKX:\n%1")
                .arg(QString::fromStdString(result.error)));
        return;
    }

    // Display in viewport as overlay
    gl_widget_->loadHkxOverlay(result);

    // Count geometry
    int totalTris = 0;
    for (auto& pd : result.physicsData)
        for (auto& sys : pd.systems)
            for (auto& rb : sys.rigidBodies) {
                std::function<void(const Hkx::ShapeInfo&)> count;
                count = [&](const Hkx::ShapeInfo& s) {
                    totalTris += static_cast<int>(s.triangles.size());
                    for (auto& c : s.children) count(c);
                };
                count(rb.shape);
            }

    statusBar()->showMessage(
        QString("HKX generated: %1 collision triangles — toggle View > Show HKX/NIF to compare")
            .arg(totalTris), 10000);
}

void MainWindow::onSaveHkx()
{
    if (!hasNif_) {
        QMessageBox::warning(this, "No NIF", "Load a NIF file first.");
        return;
    }

    QSettings settings;
    QString lastDir = settings.value("last_open_dir").toString();

    QString path = qt_common::FileBrowserDialog::getSaveFileName(this,
        "Save HKX File", lastDir,
        "Havok Files (*.hkx);;All Files (*)");

    if (path.isEmpty()) return;
    if (!path.endsWith(".hkx", Qt::CaseInsensitive))
        path += ".hkx";

    if (Hkx::writeNifAsHkx(currentNif_, path.toStdString())) {
        statusBar()->showMessage(QString("Saved: %1").arg(path), 5000);
    } else {
        QMessageBox::warning(this, "Error", "Failed to write HKX file.");
    }
}

void MainWindow::buildTree() {
    buildingTree_ = true;
    tree_->clear();
    const auto& nif = currentNif_;

    // File info
    auto* fileItem = new QTreeWidgetItem(tree_);
    fileItem->setText(0, "NIF File");
    fileItem->setText(1, QString("v%1 | %2 blocks | user=%3/%4")
        .arg(nif.version, 0, 16).arg(nif.num_blocks)
        .arg(nif.user_version).arg(nif.user_version_2));
    fileItem->setExpanded(true);

    // Export info
    for (int i = 0; i < 3; i++) {
        if (!nif.export_info[i].empty()) {
            auto* ei = new QTreeWidgetItem(fileItem);
            ei->setText(0, i == 0 ? "Exporter" : (i == 1 ? "Version" : "Author"));
            ei->setText(1, QString::fromStdString(nif.export_info[i]));
        }
    }

    // Block types summary
    if (!nif.block_types.empty()) {
        auto* btRoot = new QTreeWidgetItem(fileItem);
        btRoot->setText(0, QString("Block Types (%1)").arg(nif.block_types.size()));
        for (size_t i = 0; i < nif.block_types.size(); i++) {
            auto* bt = new QTreeWidgetItem(btRoot);
            bt->setText(0, QString("[%1]").arg(i));
            bt->setText(1, QString::fromStdString(nif.block_types[i]));
        }
    }

    // Nodes (scene graph)
    if (!nif.nodes.empty()) {
        auto* nodesRoot = new QTreeWidgetItem(tree_);
        nodesRoot->setText(0, QString("Nodes (%1)").arg(nif.nodes.size()));
        nodesRoot->setExpanded(true);
        for (size_t i = 0; i < nif.nodes.size(); i++) {
            const auto& node = nif.nodes[i];
            auto* item = new QTreeWidgetItem(nodesRoot);
            item->setText(0, node.name.empty() ? QString("Node %1").arg(i)
                                               : QString::fromStdString(node.name));
            QStringList details;
            details << QString::fromStdString(node.type_name);
            if (!node.children.empty())
                details << QString("%1 children").arg(node.children.size());
            if (node.data_ref >= 0)
                details << QString("data=%1").arg(node.data_ref);
            item->setText(1, details.join(" | "));
        }
    }

    // Meshes
    if (!nif.meshes.empty()) {
        auto* meshRoot = new QTreeWidgetItem(tree_);
        meshRoot->setText(0, QString("Meshes (%1)").arg(nif.meshes.size()));
        meshRoot->setExpanded(true);
        for (size_t i = 0; i < nif.meshes.size(); i++) {
            const auto& mesh = nif.meshes[i];
            auto* item = new QTreeWidgetItem(meshRoot);
            item->setText(0, QString("Mesh %1").arg(i));
            // Link to render mesh for selection
            item->setData(0, Qt::UserRole + 1, static_cast<int>(i));

            QStringList details;
            details << QString::fromStdString(mesh.block_type);
            details << QString("%1 verts").arg(mesh.vertices.size());
            details << QString("%1 tris").arg(mesh.triangles.size());
            if (mesh.has_vertex_colors) details << "colors";
            if (mesh.has_tangents) details << "tangents";
            if (mesh.lod_level >= 0) details << QString("LOD %1").arg(mesh.lod_level);
            details << QString("bound r=%1").arg(static_cast<double>(mesh.bound_radius), 0, 'f', 1);
            item->setText(1, details.join(" | "));
        }
    }

    // Materials
    if (!nif.materials.empty()) {
        auto* matRoot = new QTreeWidgetItem(tree_);
        matRoot->setText(0, QString("Materials (%1)").arg(nif.materials.size()));
        for (size_t i = 0; i < nif.materials.size(); i++) {
            const auto& mat = nif.materials[i];
            auto* item = new QTreeWidgetItem(matRoot);
            item->setText(0, mat.name.empty() ? QString("Material %1").arg(i)
                                              : QString::fromStdString(mat.name));
            QStringList details;
            details << QString("alpha=%1").arg(static_cast<double>(mat.alpha), 0, 'f', 2);
            details << QString("gloss=%1").arg(static_cast<double>(mat.glossiness), 0, 'f', 1);
            item->setText(1, details.join(" | "));
        }
    }

    // Textures
    if (!nif.textures.empty()) {
        auto* texRoot = new QTreeWidgetItem(tree_);
        texRoot->setText(0, QString("Textures (%1)").arg(nif.textures.size()));
        for (size_t i = 0; i < nif.textures.size(); i++) {
            const auto& tex = nif.textures[i];
            auto* item = new QTreeWidgetItem(texRoot);
            item->setText(0, QString("Texture %1").arg(i));
            item->setText(1, tex.filename.empty()
                ? QString("(block %1)").arg(tex.block_index)
                : QString::fromStdString(tex.filename));
        }
    }

    // Animations
    if (!nif.sequences.empty()) {
        auto* animRoot = new QTreeWidgetItem(tree_);
        animRoot->setText(0, QString("Animations (%1)").arg(nif.sequences.size()));
        for (size_t i = 0; i < nif.sequences.size(); i++) {
            const auto& seq = nif.sequences[i];
            auto* item = new QTreeWidgetItem(animRoot);
            item->setText(0, QString::fromStdString(seq.name));
            item->setText(1, QString("%1 blocks, %2-%3s")
                .arg(seq.controlled_blocks.size())
                .arg(static_cast<double>(seq.start_time), 0, 'f', 2)
                .arg(static_cast<double>(seq.stop_time), 0, 'f', 2));
        }
    }

    tree_->expandToDepth(0);
    buildingTree_ = false;
}

void MainWindow::onTreeItemSelected() {
    auto* item = tree_->currentItem();
    if (!item) return;

    QVariant meshData = item->data(0, Qt::UserRole + 1);
    if (meshData.isValid()) {
        gl_widget_->setSelectedIndex(meshData.toInt());
        statusBar()->showMessage(item->text(0) + "  " + item->text(1), 5000);
    } else {
        gl_widget_->setSelectedIndex(-1);
        statusBar()->showMessage(item->text(0) + "  " + item->text(1), 5000);
    }
}
