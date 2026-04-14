#pragma once

#include "nif_gl_widget.h"

#include <QMainWindow>
#include <QLabel>
#include <QTreeWidget>
#include <QString>

#include "gamebryo/nif/nif_reader.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openFile(const QString& path);

private slots:
    void onFileOpen();
    void onFileLoaded(uint32_t blocks, uint32_t vertices, uint32_t triangles);
    void onGenerateHkx();
    void onSaveHkx();
    void onTreeItemSelected();

private:
    void buildTree();

    nif_viewer::NifGLWidget* gl_widget_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    lu::assets::NifFile currentNif_;
    bool hasNif_ = false;
    bool buildingTree_ = false;
};
