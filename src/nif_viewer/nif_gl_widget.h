#pragma once
// nif_gl_widget.h — NIF viewer GL viewport subclass.
// Renders NIF meshes (lit) and optional HKX collision overlay (unlit).

#include "gl_viewport_widget.h"
#include "gamebryo/nif/nif_reader.h"
#include "havok/types/hkx_types.h"

#include <vector>

namespace nif_viewer {

class NifGLWidget : public gl_viewport::BaseGLViewport {
    Q_OBJECT
public:
    explicit NifGLWidget(QWidget* parent = nullptr);

    bool loadNif(const std::vector<uint8_t>& data);

    // Load HKX collision data as overlay meshes
    void loadHkxOverlay(const Hkx::ParseResult& hkxResult);
    void clearHkxOverlay();

    uint32_t totalVertices() const { return total_vertices_; }
    uint32_t totalTriangles() const { return total_triangles_; }
    uint32_t blockCount() const { return block_count_; }

    bool showNif = true;
    bool showHkx = true;

signals:
    void fileLoaded(uint32_t blocks, uint32_t vertices, uint32_t triangles);

protected:
    void onInitGL() override;
    void paintGL() override;
    void drawBackground() override;


private:
    void buildMeshData(const lu::assets::NifFile& nif);

    int nifMeshCount_ = 0;   // meshes_[0..nifMeshCount_) are NIF
    // meshes_[nifMeshCount_..end) are HKX overlay

    uint32_t total_vertices_ = 0;
    uint32_t total_triangles_ = 0;
    uint32_t block_count_ = 0;
};

} // namespace nif_viewer
