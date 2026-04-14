// hkx_gl_widget.h — HKX-specific GL viewport subclass.
//
// Renders Havok collision shapes and scene meshes from parsed HKX files.
// Subclasses BaseGLViewport for shared camera, rendering, and picking.
#pragma once

#include "gl_viewport_widget.h"
#include "havok/types/hkx_types.h"

#include <vector>
#include <cstdint>

namespace hkx_viewer {

struct ViewerStats {
    int rigidBodyCount = 0;
    int shapeCount = 0;
    int totalVertices = 0;
    int totalTriangles = 0;
    int sceneMeshCount = 0;
    int sceneMeshVertices = 0;
    int sceneMeshTriangles = 0;
};

// Metadata for each render mesh — tracks HKX-specific properties.
struct HkxMeshInfo {
    Hkx::ShapeType shapeType = Hkx::ShapeType::Unknown;
    bool isSceneMesh = false;
};

class HkxGLWidget : public gl_viewport::BaseGLViewport {
    Q_OBJECT
public:
    explicit HkxGLWidget(QWidget* parent = nullptr);

    void loadParseResult(const Hkx::ParseResult& result);
    void clear();

    const ViewerStats& stats() const { return stats_; }
    const HkxMeshInfo& meshInfoAt(int idx) const { return meshInfo_[idx]; }

    // Per-shape-type visibility toggles
    bool showCollision_ = true;
    bool showSceneMeshes_ = true;
    bool showBox_ = true;
    bool showSphere_ = true;
    bool showConvex_ = true;
    bool showMesh_ = true;
    bool showCapsuleCylinder_ = true;

signals:
    void statsChanged();

protected:
    void drawBackground() override;
    bool shouldDrawMesh(int idx) const override;

private:
    // Build RenderMeshes from a single ShapeInfo (recursive for containers).
    void buildMeshesFromShape(const Hkx::ShapeInfo& shape,
                              const Hkx::Transform& parentTransform);

    // Primitive generators
    void addBox(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform);
    void addSphere(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform);
    void addCapsule(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform);
    void addCylinder(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform);
    void addConvexVertices(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform);
    void addMeshVertices(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform);
    void addTriangleMesh(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform);
    void addCompressedMesh(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform);

    // Scene mesh extraction
    void buildSceneMeshes(const Hkx::SceneInfo& scene,
                          const Hkx::Transform& rootTransform);
    void walkSceneNode(const Hkx::SceneInfo& scene, int nodeIndex,
                       const Hkx::Transform& parentTransform);

    // Color assignment
    static void shapeColor(Hkx::ShapeType type, float* rgba, float* wireRgb);

    // Per-mesh HKX metadata (parallel to meshes_)
    std::vector<HkxMeshInfo> meshInfo_;
    ViewerStats stats_;
};

} // namespace hkx_viewer
