// main_window.h — QMainWindow wrapper for the HKX collision viewer.
//
// Provides File > Open dialog, status bar with shape/vertex/triangle counts,
// a tree view of the HKX structure, and hosts the HkxGLWidget as the central widget.
#pragma once

#include "hkx_gl_widget.h"

#include <QMainWindow>
#include <QLabel>
#include <QTreeWidget>

namespace hkx_viewer {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Load and display an HKX file. Returns true on success.
    bool loadFile(const std::string& path);

private slots:
    void onOpen();
    void onStatsChanged();
    void onTreeItemSelected();

private:
    // Populate the tree view from a parse result.
    void buildTree(const Hkx::ParseResult& result);

    HkxGLWidget* glWidget_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTreeWidget* tree_ = nullptr;

    // Store the last parse result for tree building
    Hkx::ParseResult lastResult_;
    int nextMeshIdx_ = 0;
    bool buildingTree_ = false;
};

} // namespace hkx_viewer
