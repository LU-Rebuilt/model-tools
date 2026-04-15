#pragma once
// lxfml_gl_widget.h — LXFML brick model GL viewport.

#include "gl_viewport_widget.h"
#include "lego/brick_assembly/brick_assembly.h"
#include "havok/types/hkx_types.h"

#include <vector>

namespace lxfml_viewer {

class LxfmlGLWidget : public gl_viewport::BaseGLViewport {
    Q_OBJECT
public:
    explicit LxfmlGLWidget(QWidget* parent = nullptr);

    // Load assembled bricks into the viewport.
    void loadAssembly(const lu::assets::AssemblyResult& assembly);

    // Load HKX collision data as overlay meshes
    void loadHkxOverlay(const Hkx::ParseResult& hkxResult);
    void clearHkxOverlay();

    uint32_t totalVertices() const { return total_vertices_; }
    uint32_t totalTriangles() const { return total_triangles_; }

    bool showBricks = true;
    bool showHkx = true;

signals:
    void modelLoaded(uint32_t bricks, uint32_t vertices, uint32_t triangles);

protected:
    void onInitGL() override;
    void paintGL() override;
    void drawBackground() override;

private:
    int brickMeshCount_ = 0;   // meshes_[0..brickMeshCount_) are brick geometry

    uint32_t total_vertices_ = 0;
    uint32_t total_triangles_ = 0;
};

} // namespace lxfml_viewer
