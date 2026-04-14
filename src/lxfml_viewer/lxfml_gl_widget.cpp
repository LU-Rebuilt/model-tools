#include "lxfml_gl_widget.h"
#include "gl_helpers.h"

#include <QOpenGLFunctions>

namespace lxfml_viewer {

using gl_viewport::RenderMesh;

LxfmlGLWidget::LxfmlGLWidget(QWidget* parent) : BaseGLViewport(parent) {}

void LxfmlGLWidget::onInitGL() {
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);

    GLfloat light_pos[] = {1.0f, 1.0f, 1.0f, 0.0f};
    GLfloat light_amb[] = {0.3f, 0.3f, 0.3f, 1.0f};
    GLfloat light_dif[] = {0.8f, 0.8f, 0.8f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_dif);
}

void LxfmlGLWidget::loadAssembly(const lu::assets::AssemblyResult& assembly) {
    clearMeshes();
    total_vertices_ = total_triangles_ = 0;

    for (const auto& ab : assembly.bricks) {
        RenderMesh rm;
        rm.color[0] = ab.color.r;
        rm.color[1] = ab.color.g;
        rm.color[2] = ab.color.b;
        rm.color[3] = ab.color.a;
        // Wireframe: lighten dark bricks, darken light ones
        float lum = ab.color.r * 0.299f + ab.color.g * 0.587f + ab.color.b * 0.114f;
        if (lum < 0.25f) {
            rm.wireColor[0] = ab.color.r + 0.3f;
            rm.wireColor[1] = ab.color.g + 0.3f;
            rm.wireColor[2] = ab.color.b + 0.3f;
        } else {
            rm.wireColor[0] = ab.color.r * 0.3f;
            rm.wireColor[1] = ab.color.g * 0.3f;
            rm.wireColor[2] = ab.color.b * 0.3f;
        }
        rm.label = ab.label;
        rm.vertices = ab.vertices;
        rm.normals = ab.normals;
        rm.indices = ab.indices;
        addMesh(std::move(rm));
    }

    total_vertices_ = assembly.total_vertices;
    total_triangles_ = assembly.total_triangles;

    fitToVisible();
    emit modelLoaded(assembly.bricks_loaded, total_vertices_, total_triangles_);
}

void LxfmlGLWidget::paintGL() {
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

    // Pass 1: Solid fill with lighting and per-brick color
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glEnable(GL_LIGHTING);

    for (int mi = 0; mi < static_cast<int>(meshes_.size()); ++mi) {
        const auto& mesh = meshes_[mi];
        if (mesh.vertices.empty() || mesh.indices.empty() || !mesh.visible) continue;

        bool selected = (mi == selectedIdx_);

        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, mesh.vertices.data());

        if (!mesh.normals.empty()) {
            glEnableClientState(GL_NORMAL_ARRAY);
            glNormalPointer(GL_FLOAT, 0, mesh.normals.data());
        }

        // Transparent bricks
        bool transparent = mesh.color[3] < 0.99f;
        if (transparent) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(selected ? 1.0f : mesh.color[0],
                      selected ? 0.5f : mesh.color[1],
                      selected ? 0.2f : mesh.color[2],
                      mesh.color[3]);
        } else {
            if (selected) glColor3f(1.0f, 0.5f, 0.2f);
            else glColor3f(mesh.color[0], mesh.color[1], mesh.color[2]);
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                       GL_UNSIGNED_INT, mesh.indices.data());

        if (transparent) glDisable(GL_BLEND);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    glDisable(GL_POLYGON_OFFSET_FILL);

    // Pass 2: Wireframe overlay
    if (showWireframe) {
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        for (int mi = 0; mi < static_cast<int>(meshes_.size()); ++mi) {
            const auto& mesh = meshes_[mi];
            if (mesh.vertices.empty() || mesh.indices.empty() || !mesh.visible) continue;

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

void LxfmlGLWidget::drawBackground() {
    gl_viewport::drawGrid(50.0f, 5.0f);
    gl_viewport::drawAxes(3.0f);
}

} // namespace lxfml_viewer
