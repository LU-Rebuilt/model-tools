// main_window.cpp — MainWindow implementation for the HKX collision viewer.

#include "main_window.h"

#include "havok/reader/hkx_reader.h"

#include <QMenuBar>
#include <QDockWidget>
#include "file_browser.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>

#include <filesystem>

namespace hkx_viewer {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("HKX Collision Viewer");
    resize(1280, 800);

    // Central widget
    glWidget_ = new HkxGLWidget(this);
    setCentralWidget(glWidget_);

    // Menu bar
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* openAct = fileMenu->addAction("&Open...", this, &MainWindow::onOpen);
    openAct->setShortcut(QKeySequence::Open);

    fileMenu->addSeparator();

    auto* quitAct = fileMenu->addAction("&Quit", this, &QWidget::close);
    quitAct->setShortcut(QKeySequence::Quit);

    auto* viewMenu = menuBar()->addMenu("&View");

    auto* showWireAct = viewMenu->addAction("Show &Wireframe");
    showWireAct->setCheckable(true);
    showWireAct->setChecked(true);
    connect(showWireAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showWireframe = checked;
        glWidget_->update();
    });

    auto* showSolidAct = viewMenu->addAction("Show S&olid Fill");
    showSolidAct->setCheckable(true);
    showSolidAct->setChecked(true);
    connect(showSolidAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showSolid = checked;
        glWidget_->update();
    });

    viewMenu->addSeparator();

    auto* showBoxAct = viewMenu->addAction("Show &Box Shapes");
    showBoxAct->setCheckable(true);
    showBoxAct->setChecked(true);
    connect(showBoxAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showBox_ = checked;
        glWidget_->update();
    });

    auto* showSphereAct = viewMenu->addAction("Show S&phere Shapes");
    showSphereAct->setCheckable(true);
    showSphereAct->setChecked(true);
    connect(showSphereAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showSphere_ = checked;
        glWidget_->update();
    });

    auto* showConvexAct = viewMenu->addAction("Show Con&vex Shapes");
    showConvexAct->setCheckable(true);
    showConvexAct->setChecked(true);
    connect(showConvexAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showConvex_ = checked;
        glWidget_->update();
    });

    auto* showMeshAct = viewMenu->addAction("Show &Mesh Shapes");
    showMeshAct->setCheckable(true);
    showMeshAct->setChecked(true);
    connect(showMeshAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showMesh_ = checked;
        glWidget_->update();
    });

    auto* showCapCylAct = viewMenu->addAction("Show Capsule/C&ylinder");
    showCapCylAct->setCheckable(true);
    showCapCylAct->setChecked(true);
    connect(showCapCylAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showCapsuleCylinder_ = checked;
        glWidget_->update();
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&Color Legend", [this]() {
        QMessageBox::information(this, "HKX Viewer — Color Legend",
            "<h3>Collision Shapes</h3>"
            "<ul>"
            "<li><b style='color:#4466ff'>Blue</b> — Box (hkpBoxShape)</li>"
            "<li><b style='color:#33ff55'>Green</b> — Sphere (hkpSphereShape)</li>"
            "<li><b style='color:#ffee33'>Yellow</b> — Capsule / Cylinder</li>"
            "<li><b style='color:#33ccdd'>Cyan</b> — Convex Vertices (hkpConvexVerticesShape)</li>"
            "<li><b style='color:#ff6644'>Red/Orange</b> — Triangle Mesh / Compressed Mesh</li>"
            "<li><b style='color:#cc88ff'>Purple</b> — Extended Mesh / Simple Container</li>"
            "</ul>"
            "<h3>Scene Meshes</h3>"
            "<ul>"
            "<li><b style='color:#b3a899'>Warm Grey</b> — hkxScene visual geometry (hkxMesh)</li>"
            "</ul>"
            "<h3>Controls</h3>"
            "<ul>"
            "<li><b>Left-click</b> — Select mesh (highlights orange)</li>"
            "<li><b>Left-drag</b> — Orbit camera</li>"
            "<li><b>Right-drag</b> — Pan camera</li>"
            "<li><b>Scroll wheel</b> — Zoom in/out</li>"
            "</ul>"
        );
    });
    helpMenu->addAction("&About", [this]() {
        QMessageBox::about(this, "About HKX Viewer",
            "<b>HKX Collision Viewer</b><br>"
            "Part of the LU-Rebuilt project<br><br>"
            "Displays Havok physics collision shapes and scene meshes.<br>"
            "Supports both binary packfile and tagged binary HKX formats.");
    });

    // ---- Tree dock (right side) ----

    auto* treeDock = new QDockWidget("HKX Structure");
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Name", "Details"});
    tree_->setColumnWidth(0, 250);
    tree_->setAlternatingRowColors(true);
    treeDock->setWidget(tree_);
    treeDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, treeDock);

    connect(tree_, &QTreeWidget::currentItemChanged,
            this, [this]() { onTreeItemSelected(); });
    connect(tree_, &QTreeWidget::itemChanged,
            this, [this](QTreeWidgetItem* item, int /*column*/) {
                if (buildingTree_) return;

                bool checked = (item->checkState(0) == Qt::Checked);

                // If this item has a mesh index, toggle that mesh
                if (item->data(0, Qt::UserRole + 1).isValid()) {
                    int meshIdx = item->data(0, Qt::UserRole + 1).toInt();
                    if (meshIdx >= 0 && meshIdx < glWidget_->meshCount())
                        glWidget_->setMeshVisible(meshIdx, checked);
                }

                // Propagate to all children (parent checkbox toggles children)
                buildingTree_ = true; // prevent recursion
                std::function<void(QTreeWidgetItem*, bool)> setChildren =
                    [&](QTreeWidgetItem* parent, bool state) {
                        for (int i = 0; i < parent->childCount(); i++) {
                            auto* child = parent->child(i);
                            if (child->flags() & Qt::ItemIsUserCheckable)
                                child->setCheckState(0, state ? Qt::Checked : Qt::Unchecked);
                            if (child->data(0, Qt::UserRole + 1).isValid()) {
                                int idx = child->data(0, Qt::UserRole + 1).toInt();
                                if (idx >= 0 && idx < glWidget_->meshCount())
                                    glWidget_->setMeshVisible(idx, state);
                            }
                            setChildren(child, state);
                        }
                    };
                setChildren(item, checked);

                // Update parent check state based on children
                if (item->parent()) {
                    auto* p = item->parent();
                    if (p->flags() & Qt::ItemIsUserCheckable) {
                        bool anyChecked = false, allChecked = true;
                        for (int i = 0; i < p->childCount(); i++) {
                            auto* c = p->child(i);
                            if (!(c->flags() & Qt::ItemIsUserCheckable)) continue;
                            if (c->checkState(0) == Qt::Checked) anyChecked = true;
                            else allChecked = false;
                        }
                        p->setCheckState(0, allChecked ? Qt::Checked
                            : (anyChecked ? Qt::PartiallyChecked : Qt::Unchecked));
                    }
                }

                buildingTree_ = false;
            });

    // Status bar
    statusLabel_ = new QLabel("No file loaded");
    statusBar()->addWidget(statusLabel_, 1);

    connect(glWidget_, &HkxGLWidget::statsChanged,
            this, &MainWindow::onStatsChanged);
    connect(glWidget_, &HkxGLWidget::meshClicked,
            this, [this](int index) {
                if (index >= 0) {
                    QString label = QString::fromStdString(glWidget_->meshAt(index).label);
                    statusBar()->showMessage(QString("Selected [%1]: %2").arg(index).arg(label));
                    // Find and select the corresponding tree item
                    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> findItem =
                        [&](QTreeWidgetItem* parent) -> QTreeWidgetItem* {
                            for (int i = 0; i < parent->childCount(); i++) {
                                auto* child = parent->child(i);
                                QVariant d = child->data(0, Qt::UserRole + 1);
                                if (d.isValid() && d.toInt() == index)
                                    return child;
                                auto* found = findItem(child);
                                if (found) return found;
                            }
                            return nullptr;
                        };
                    auto* item = findItem(tree_->invisibleRootItem());
                    if (item) {
                        tree_->blockSignals(true);
                        tree_->setCurrentItem(item);
                        tree_->scrollToItem(item);
                        tree_->blockSignals(false);
                    }
                } else {
                    statusBar()->clearMessage();
                }
            });
}

bool MainWindow::loadFile(const std::string& path) {
    Hkx::HkxFile parser;
    auto result = parser.Parse(std::filesystem::path(path));

    if (!result.success) {
        QMessageBox::warning(this, "Parse Error",
            QString("Failed to parse %1:\n%2")
                .arg(QString::fromStdString(path))
                .arg(QString::fromStdString(result.error)));
        return false;
    }

    glWidget_->loadParseResult(result);
    buildTree(result);
    lastResult_ = std::move(result);
    setWindowTitle(QString("HKX Collision Viewer - %1")
        .arg(QString::fromStdString(std::filesystem::path(path).filename().string())));

    return true;
}

void MainWindow::onOpen() {
    QSettings settings;
    QString lastDir = settings.value("last_open_dir").toString();

    QString path = qt_common::FileBrowserDialog::getOpenFileName(
        this,
        "Open HKX File",
        lastDir,
        "Havok Files (*.hkx *.xml);;HKX Binary (*.hkx);;HKX XML (*.xml);;All Files (*)");

    if (!path.isEmpty()) {
        settings.setValue("last_open_dir",
            QFileInfo(path).absolutePath());
        loadFile(path.toStdString());
    }
}

void MainWindow::onStatsChanged() {
    auto& s = glWidget_->stats();
    QString text = QString("Rigid bodies: %1  |  Shapes: %2  |  Vertices: %3  |  Triangles: %4")
        .arg(s.rigidBodyCount)
        .arg(s.shapeCount)
        .arg(s.totalVertices)
        .arg(s.totalTriangles);
    if (s.sceneMeshCount > 0) {
        text += QString("  |  Scene meshes: %1 (%2 verts, %3 tris)")
            .arg(s.sceneMeshCount)
            .arg(s.sceneMeshVertices)
            .arg(s.sceneMeshTriangles);
    }
    statusLabel_->setText(text);
}

// ---------------------------------------------------------------------------
// Tree view — mirrors the HKX object graph structure
// ---------------------------------------------------------------------------

static QString shapeTypeName(Hkx::ShapeType type) {
    switch (type) {
        case Hkx::ShapeType::Box: return "hkpBoxShape";
        case Hkx::ShapeType::Sphere: return "hkpSphereShape";
        case Hkx::ShapeType::Capsule: return "hkpCapsuleShape";
        case Hkx::ShapeType::Cylinder: return "hkpCylinderShape";
        case Hkx::ShapeType::ConvexVertices: return "hkpConvexVerticesShape";
        case Hkx::ShapeType::Triangle: return "hkpTriangleShape";
        case Hkx::ShapeType::CompressedMesh: return "hkpCompressedMeshShape";
        case Hkx::ShapeType::ExtendedMesh: return "hkpExtendedMeshShape";
        case Hkx::ShapeType::SimpleContainer: return "hkpSimpleContainer";
        case Hkx::ShapeType::Mopp: return "hkpMoppBvTreeShape";
        case Hkx::ShapeType::BvTree: return "hkpBvTreeShape";
        case Hkx::ShapeType::List: return "hkpListShape";
        case Hkx::ShapeType::Transform: return "hkpTransformShape";
        case Hkx::ShapeType::ConvexTransform: return "hkpConvexTransformShape";
        case Hkx::ShapeType::ConvexTranslate: return "hkpConvexTranslateShape";
        default: return "Unknown";
    }
}

static void addShapeToTreeImpl(QTreeWidgetItem* parent, const Hkx::ShapeInfo& shape) {
    auto* item = new QTreeWidgetItem(parent);
    QString name = shapeTypeName(shape.type);
    if (!shape.className.empty())
        name = QString::fromStdString(shape.className);
    item->setText(0, name);

    QStringList details;
    if (shape.numVertices > 0) details << QString("%1 verts").arg(shape.numVertices);
    if (shape.numTriangles > 0 || !shape.triangles.empty())
        details << QString("%1 tris").arg(shape.numTriangles > 0 ? shape.numTriangles : static_cast<int>(shape.triangles.size()));
    if (!shape.planeEquations.empty()) details << QString("%1 planes").arg(shape.planeEquations.size());
    if (shape.type == Hkx::ShapeType::Box)
        details << QString("half=(%1,%2,%3)")
            .arg(static_cast<double>(shape.halfExtents.x), 0, 'f', 2)
            .arg(static_cast<double>(shape.halfExtents.y), 0, 'f', 2)
            .arg(static_cast<double>(shape.halfExtents.z), 0, 'f', 2);
    if (shape.type == Hkx::ShapeType::Sphere)
        details << QString("r=%1").arg(static_cast<double>(shape.radius), 0, 'f', 2);
    if (shape.type == Hkx::ShapeType::Capsule || shape.type == Hkx::ShapeType::Cylinder)
        details << QString("r=%1").arg(static_cast<double>(shape.radius), 0, 'f', 2);
    if (shape.compressedMesh)
        details << QString("%1 chunks, %2 bigTris").arg(shape.compressedMesh->chunks.size())
            .arg(shape.compressedMesh->bigTriangles.size());
    item->setText(1, details.join(" | "));

    for (const auto& child : shape.children)
        addShapeToTreeImpl(item, child);
}

static void makeCheckable(QTreeWidgetItem* item, bool checked = true) {
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
}

// Helper: build a tree for a scene node hierarchy (recursive)
static void addSceneNodeToTree(QTreeWidgetItem* parent, const Hkx::SceneInfo& scene,
                                int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(scene.nodes.size())) return;
    const auto& node = scene.nodes[nodeIndex];

    auto* item = new QTreeWidgetItem(parent);
    QString name = node.name.empty() ? QString("Node %1").arg(nodeIndex)
                                     : QString::fromStdString(node.name);
    item->setText(0, name);

    QStringList details;
    if (node.meshIndex >= 0 && node.meshIndex < static_cast<int>(scene.meshes.size())) {
        const auto& mesh = scene.meshes[node.meshIndex];
        details << QString("mesh %1: %2 verts, %3 tris")
            .arg(node.meshIndex).arg(mesh.vertices.size()).arg(mesh.triangles.size());
    }
    if (!node.childIndices.empty())
        details << QString("%1 children").arg(node.childIndices.size());
    auto& t = node.transform;
    if (std::abs(t.translation.x) > 0.01f || std::abs(t.translation.y) > 0.01f ||
        std::abs(t.translation.z) > 0.01f)
        details << QString("T=(%1,%2,%3)")
            .arg(static_cast<double>(t.translation.x), 0, 'f', 1)
            .arg(static_cast<double>(t.translation.y), 0, 'f', 1)
            .arg(static_cast<double>(t.translation.z), 0, 'f', 1);
    item->setText(1, details.join(" | "));

    for (int childIdx : node.childIndices)
        addSceneNodeToTree(item, scene, childIdx);
}

// Helper: add rigid body to tree
static void addRigidBodyToTree(QTreeWidgetItem* parent, const Hkx::RigidBodyInfo& rb, int index) {
    auto* rbItem = new QTreeWidgetItem(parent);
    QString name = QString("hkpRigidBody %1").arg(index);
    rbItem->setText(0, name);
    rbItem->setText(1, QString("pos=(%1,%2,%3) mass=%4 | %5")
        .arg(static_cast<double>(rb.position.x), 0, 'f', 1)
        .arg(static_cast<double>(rb.position.y), 0, 'f', 1)
        .arg(static_cast<double>(rb.position.z), 0, 'f', 1)
        .arg(static_cast<double>(rb.mass))
        .arg(QString::fromStdString(rb.shape.className)));
    makeCheckable(rbItem);
    if (rb.shape.type != Hkx::ShapeType::Unknown)
        addShapeToTreeImpl(rbItem, rb.shape);
}

void MainWindow::buildTree(const Hkx::ParseResult& result) {
    buildingTree_ = true;
    tree_->clear();

    // ---- hkRootLevelContainer (entry point — all data nests under it) ----
    QTreeWidgetItem* rootParent = tree_->invisibleRootItem();
    auto* rcRoot = new QTreeWidgetItem(tree_);
    rcRoot->setText(0, "hkRootLevelContainer");
    rcRoot->setText(1, QString("Havok %1 | v%2")
        .arg(QString::fromStdString(result.havokVersion))
        .arg(result.fileVersion));
    rcRoot->setExpanded(true);
    rootParent = rcRoot;

    // Build className → variant name map so structural nodes use friendly names
    std::unordered_map<std::string, std::string> variantNames;
    for (const auto& rc : result.rootContainers)
        for (const auto& nv : rc.namedVariants)
            variantNames[nv.className] = nv.name;

    // Any classes not referenced by named variants get listed as info-only
    if (!result.rootContainers.empty()) {
        for (const auto& rc : result.rootContainers) {
            for (const auto& nv : rc.namedVariants) {
                // Skip classes that will have structural nodes below
                if (nv.className == "hkpPhysicsData" || nv.className == "hkxScene" ||
                    nv.className == "hkpPhysicsSystem") continue;
                auto* nvItem = new QTreeWidgetItem(rcRoot);
                nvItem->setText(0, QString::fromStdString(nv.name));
                nvItem->setText(1, QString::fromStdString(nv.className));
            }
        }
    }

    // ---- Physics Data → Systems → Rigid Bodies → Shapes ----
    for (size_t pi = 0; pi < result.physicsData.size(); pi++) {
        const auto& pd = result.physicsData[pi];
        auto* pdItem = new QTreeWidgetItem(rootParent);
        auto pdNameIt = variantNames.find("hkpPhysicsData");
        pdItem->setText(0, pdNameIt != variantNames.end()
            ? QString::fromStdString(pdNameIt->second) : "hkpPhysicsData");
        pdItem->setText(1, QString("%1 systems").arg(pd.systems.size()));
        makeCheckable(pdItem);
        pdItem->setExpanded(true);
        for (size_t si = 0; si < pd.systems.size(); si++) {
            const auto& sys = pd.systems[si];
            auto* sysItem = new QTreeWidgetItem(pdItem);
            sysItem->setText(0, QString("hkpPhysicsSystem %1").arg(si));
            sysItem->setText(1, QString("%1 rigid bodies").arg(sys.rigidBodies.size()));
            makeCheckable(sysItem);
            sysItem->setExpanded(true);
            for (size_t ri = 0; ri < sys.rigidBodies.size(); ri++)
                addRigidBodyToTree(sysItem, sys.rigidBodies[ri], ri);
        }
    }

    // Standalone physics systems — only show if NOT already covered by physicsData
    if (result.physicsData.empty()) {
        for (size_t si = 0; si < result.physicsSystems.size(); si++) {
            const auto& sys = result.physicsSystems[si];
            auto* sysItem = new QTreeWidgetItem(rootParent);
            sysItem->setText(0, QString("hkpPhysicsSystem %1").arg(si));
            sysItem->setText(1, QString("%1 rigid bodies").arg(sys.rigidBodies.size()));
            makeCheckable(sysItem);
            for (size_t ri = 0; ri < sys.rigidBodies.size(); ri++)
                addRigidBodyToTree(sysItem, sys.rigidBodies[ri], ri);
        }
    }

    // Top-level rigid bodies — only show if NOT already in a system
    if (result.physicsData.empty() && result.physicsSystems.empty() && !result.rigidBodies.empty()) {
        auto* rbRoot = new QTreeWidgetItem(rootParent);
        rbRoot->setText(0, QString("Rigid Bodies (%1)").arg(result.rigidBodies.size()));
        makeCheckable(rbRoot);
        rbRoot->setExpanded(true);
        for (size_t ri = 0; ri < result.rigidBodies.size(); ri++)
            addRigidBodyToTree(rbRoot, result.rigidBodies[ri], ri);
    }

    // ---- hkxScene → Node hierarchy (tree structure, not flat list) ----
    for (size_t si = 0; si < result.scenes.size(); si++) {
        const auto& scene = result.scenes[si];
        auto* sceneItem = new QTreeWidgetItem(rootParent);
        auto scNameIt = variantNames.find("hkxScene");
        sceneItem->setText(0, scNameIt != variantNames.end()
            ? QString::fromStdString(scNameIt->second) : "hkxScene");
        sceneItem->setText(1, QString("%1 meshes, %2 nodes | %3")
            .arg(scene.meshes.size()).arg(scene.nodes.size())
            .arg(QString::fromStdString(scene.modeller)));
        makeCheckable(sceneItem);
        sceneItem->setExpanded(true);

        // Node hierarchy — walk from root, showing actual parent-child tree
        if (scene.rootNodeIndex >= 0)
            addSceneNodeToTree(sceneItem, scene, scene.rootNodeIndex);
    }

    // ---- Attach render mesh checkboxes to structural tree ----
    // Find the physics and scene parents by searching recursively
    std::function<QTreeWidgetItem*(QTreeWidgetItem*, const QString&)> findChild;
    findChild = [&](QTreeWidgetItem* parent, const QString& prefix) -> QTreeWidgetItem* {
        for (int i = 0; i < parent->childCount(); i++) {
            if (parent->child(i)->text(0).startsWith(prefix)) return parent->child(i);
            auto* found = findChild(parent->child(i), prefix);
            if (found) return found;
        }
        return nullptr;
    };
    QTreeWidgetItem* physParent = findChild(rcRoot, "Physics Data");
    if (!physParent) physParent = findChild(rcRoot, "hkpPhysicsData");
    if (!physParent) physParent = findChild(rcRoot, "hkpPhysicsSystem");
    if (!physParent) physParent = findChild(rcRoot, "Rigid");
    QTreeWidgetItem* sceneParent = findChild(rcRoot, "Scene Data");
    if (!sceneParent) sceneParent = findChild(rcRoot, "hkxScene");

    for (int mi = 0; mi < glWidget_->meshCount(); mi++) {
        const auto& rm = glWidget_->meshAt(mi);
        bool isScene = glWidget_->meshInfoAt(mi).isSceneMesh;
        auto* parent = isScene
            ? (sceneParent ? sceneParent : rcRoot)
            : (physParent ? physParent : rcRoot);

        auto* meshItem = new QTreeWidgetItem(parent);
        meshItem->setText(0, QString("[%1]").arg(mi));
        meshItem->setText(1, QString::fromStdString(rm.label));
        meshItem->setData(0, Qt::UserRole + 1, mi);
        meshItem->setFlags(meshItem->flags() | Qt::ItemIsUserCheckable);
        meshItem->setCheckState(0, rm.visible ? Qt::Checked : Qt::Unchecked);
    }

    tree_->expandToDepth(1);
    buildingTree_ = false;
}

void MainWindow::onTreeItemSelected() {
    auto* item = tree_->currentItem();
    if (!item) return;

    // If this tree item maps to a render mesh, highlight it in the viewport.
    // Mesh items have UserRole set to their index; non-mesh items don't have it set.
    QVariant meshData = item->data(0, Qt::UserRole + 1); // use +1 to avoid default 0
    if (meshData.isValid()) {
        int meshIdx = meshData.toInt();
        if (meshIdx >= 0 && meshIdx < glWidget_->meshCount()) {
            glWidget_->setSelectedIndex(meshIdx);
            statusBar()->showMessage(QString("Selected [%1]: %2")
                .arg(meshIdx).arg(QString::fromStdString(glWidget_->meshAt(meshIdx).label)));
            return;
        }
    }
    glWidget_->setSelectedIndex(-1);
    statusBar()->showMessage(item->text(0) + "  " + item->text(1), 5000);
}

} // namespace hkx_viewer
