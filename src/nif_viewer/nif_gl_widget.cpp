#include "nif_gl_widget.h"
#include "gl_helpers.h"
#include "gamebryo/nif/nif_geometry.h"
#include "havok/converters/hkx_geometry.h"

#include <QOpenGLFunctions>
#include <cmath>

namespace nif_viewer {

using gl_viewport::RenderMesh;

NifGLWidget::NifGLWidget(QWidget* parent) : BaseGLViewport(parent) {}

void NifGLWidget::onInitGL() {
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);

    GLfloat light_pos[] = {1.0f, 1.0f, 1.0f, 0.0f};
    GLfloat light_amb[] = {0.25f, 0.25f, 0.25f, 1.0f};
    GLfloat light_dif[] = {0.8f, 0.8f, 0.8f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_dif);
}

bool NifGLWidget::loadNif(const std::vector<uint8_t>& data) {
    try {
        auto nif = lu::assets::nif_parse(std::span<const uint8_t>(data.data(), data.size()));
        buildMeshData(nif);
        block_count_ = nif.num_blocks;
        fitToVisible();
        emit fileLoaded(block_count_, total_vertices_, total_triangles_);
        return true;
    } catch (...) {
        clearMeshes();
        nifMeshCount_ = 0;
        total_vertices_ = total_triangles_ = block_count_ = 0;
        update();
        return false;
    }
}

void NifGLWidget::buildMeshData(const lu::assets::NifFile& nif) {
    clearMeshes();
    nifMeshCount_ = 0;
    total_vertices_ = total_triangles_ = 0;

    auto extraction = lu::assets::extractNifGeometry(nif);
    total_vertices_ = extraction.total_vertices;
    total_triangles_ = extraction.total_triangles;

    for (auto& em : extraction.meshes) {
        RenderMesh rm;
        rm.color[0] = 0.6f; rm.color[1] = 0.65f; rm.color[2] = 0.7f; rm.color[3] = 1.0f;
        rm.wireColor[0] = 0.1f; rm.wireColor[1] = 0.1f; rm.wireColor[2] = 0.1f;
        rm.label = "NIF: " + em.name;
        rm.vertices = std::move(em.vertices);
        rm.normals = std::move(em.normals);
        rm.indices = std::move(em.indices);
        addMesh(std::move(rm));
    }
    nifMeshCount_ = meshCount();
}

void NifGLWidget::loadHkxOverlay(const Hkx::ParseResult& hkxResult) {
    // Remove any previous HKX overlay meshes
    clearHkxOverlay();

    // Extract geometry from the HKX result
    auto geo = Hkx::extractGeometry(hkxResult);

    for (auto& em : geo.meshes) {
        if (em.vertices.empty() || em.indices.empty()) continue;
        RenderMesh rm;
        // Collision meshes: semi-transparent red/orange
        rm.color[0] = 1.0f; rm.color[1] = 0.4f; rm.color[2] = 0.2f; rm.color[3] = 0.4f;
        rm.wireColor[0] = 1.0f; rm.wireColor[1] = 0.5f; rm.wireColor[2] = 0.2f;
        rm.label = "HKX: " + em.label;
        rm.vertices = std::move(em.vertices);
        rm.indices = std::move(em.indices);
        addMesh(std::move(rm));
    }

    update();
}

void NifGLWidget::clearHkxOverlay() {
    // Remove meshes from nifMeshCount_ to end
    while (meshCount() > nifMeshCount_)
        removeMesh(meshCount() - 1);
    update();
}

void NifGLWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    camera().applyGLTransform();

    glDisable(GL_LIGHTING);
    drawBackground();
    glEnable(GL_LIGHTING);

    GLfloat light_pos[] = {1.0f, 1.0f, 1.0f, 0.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);

    if (meshes_.empty()) return;

    // Pass 1: Solid fill
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);

    for (int mi = 0; mi < static_cast<int>(meshes_.size()); ++mi) {
        const auto& mesh = meshes_[mi];
        if (mesh.vertices.empty() || mesh.indices.empty() || !mesh.visible) continue;

        bool isNif = (mi < nifMeshCount_);
        if (isNif && !showNif) continue;
        if (!isNif && !showHkx) continue;

        bool selected = (mi == selectedIdx_);

        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, mesh.vertices.data());

        if (isNif) {
            // NIF: lit rendering with normals
            glEnable(GL_LIGHTING);
            if (!mesh.normals.empty()) {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, 0, mesh.normals.data());
            }
            if (selected) glColor3f(1.0f, 0.5f, 0.2f);
            else glColor3f(mesh.color[0], mesh.color[1], mesh.color[2]);
        } else {
            // HKX: unlit, semi-transparent
            glDisable(GL_LIGHTING);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (selected) glColor4f(1.0f, 0.8f, 0.1f, 0.6f);
            else glColor4f(mesh.color[0], mesh.color[1], mesh.color[2], mesh.color[3]);
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                       GL_UNSIGNED_INT, mesh.indices.data());

        if (!isNif) glDisable(GL_BLEND);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    glDisable(GL_POLYGON_OFFSET_FILL);

    // Pass 2: Wireframe overlay (always unlit)
    if (showWireframe) {
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        for (int mi = 0; mi < static_cast<int>(meshes_.size()); ++mi) {
            const auto& mesh = meshes_[mi];
            if (mesh.vertices.empty() || mesh.indices.empty() || !mesh.visible) continue;

            bool isNif = (mi < nifMeshCount_);
            if (isNif && !showNif) continue;
            if (!isNif && !showHkx) continue;

            bool selected = (mi == selectedIdx_);
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, mesh.vertices.data());
            if (selected) glColor3f(1.0f, 1.0f, 0.0f);
            else glColor3f(mesh.wireColor[0], mesh.wireColor[1], mesh.wireColor[2]);
            glLineWidth(selected ? 2.0f : 1.0f);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                           GL_UNSIGNED_INT, mesh.indices.data());
            glLineWidth(1.0f);
            glDisableClientState(GL_VERTEX_ARRAY);
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glEnable(GL_LIGHTING);
}

void NifGLWidget::drawBackground() {
    gl_viewport::drawGrid(50.0f, 5.0f);
    gl_viewport::drawAxes(3.0f);
}

} // namespace nif_viewer
