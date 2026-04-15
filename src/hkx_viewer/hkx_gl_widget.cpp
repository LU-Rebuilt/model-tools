// hkx_gl_widget.cpp — HKX-specific GL viewport: shape tessellation + scene mesh rendering.
// Subclasses BaseGLViewport for shared camera, rendering, and picking.

#include "hkx_gl_widget.h"
#include "gl_helpers.h"
#include "transform_math.h"
#include "common/primitives/primitives.h"

#include <QOpenGLFunctions>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hkx_viewer {

using gl_viewport::RenderMesh;

// Convert Hkx::Transform → gl_viewport::Transform (same layout, different types)
static gl_viewport::Transform toGl(const Hkx::Transform& t) {
    gl_viewport::Transform g;
    g.col0 = {t.col0.x, t.col0.y, t.col0.z};
    g.col1 = {t.col1.x, t.col1.y, t.col1.z};
    g.col2 = {t.col2.x, t.col2.y, t.col2.z};
    g.translation = {t.translation.x, t.translation.y, t.translation.z, t.translation.w};
    return g;
}

static std::array<float, 3> transformPoint(const Hkx::Transform& t, float x, float y, float z) {
    return gl_viewport::transformPoint(toGl(t), x, y, z);
}

static Hkx::Transform combineTransforms(const Hkx::Transform& parent, const Hkx::Transform& child) {
    auto r = gl_viewport::combineTransforms(toGl(parent), toGl(child));
    Hkx::Transform result;
    result.col0 = {r.col0.x, r.col0.y, r.col0.z, 0};
    result.col1 = {r.col1.x, r.col1.y, r.col1.z, 0};
    result.col2 = {r.col2.x, r.col2.y, r.col2.z, 0};
    result.translation = {r.translation.x, r.translation.y, r.translation.z, r.translation.w};
    return result;
}

static void collectShapeOffsets(const Hkx::ShapeInfo& shape, std::set<uint32_t>& offsets) {
    if (shape.dataOffset != 0) offsets.insert(shape.dataOffset);
    for (const auto& child : shape.children) collectShapeOffsets(child, offsets);
}

HkxGLWidget::HkxGLWidget(QWidget* parent) : BaseGLViewport(parent) {}

void HkxGLWidget::clear() {
    clearMeshes();
    meshInfo_.clear();
    stats_ = {};
    emit statsChanged();
}

void HkxGLWidget::drawBackground() {
    gl_viewport::drawGrid(50.0f, 5.0f);
    gl_viewport::drawAxes(3.0f);
}

bool HkxGLWidget::shouldDrawMesh(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(meshInfo_.size())) return true;
    const auto& info = meshInfo_[idx];
    if (info.isSceneMesh) return showSceneMeshes_;
    if (!showCollision_) return false;
    using ST = Hkx::ShapeType;
    if (info.shapeType == ST::Box && !showBox_) return false;
    if (info.shapeType == ST::Sphere && !showSphere_) return false;
    if (info.shapeType == ST::ConvexVertices && !showConvex_) return false;
    if ((info.shapeType == ST::SimpleContainer || info.shapeType == ST::ExtendedMesh ||
         info.shapeType == ST::Triangle || info.shapeType == ST::CompressedMesh) && !showMesh_) return false;
    if ((info.shapeType == ST::Capsule || info.shapeType == ST::Cylinder) && !showCapsuleCylinder_) return false;
    return true;
}

void HkxGLWidget::loadParseResult(const Hkx::ParseResult& result) {
    clearMeshes();
    meshInfo_.clear();
    stats_ = {};

    std::set<uint32_t> renderedOffsets;
    std::vector<const Hkx::RigidBodyInfo*> bodies;
    for (auto& sys : result.physicsSystems) for (auto& rb : sys.rigidBodies) bodies.push_back(&rb);
    for (auto& pd : result.physicsData) for (auto& sys : pd.systems) for (auto& rb : sys.rigidBodies) bodies.push_back(&rb);
    for (auto& rb : result.rigidBodies) bodies.push_back(&rb);
    // Dedup by dataOffset — only for binary data where offsets are meaningful
    bool hasOffsets = false;
    for (auto* b : bodies) if (b->dataOffset != 0) { hasOffsets = true; break; }
    if (hasOffsets) {
        std::sort(bodies.begin(), bodies.end(), [](auto* a, auto* b) { return a->dataOffset < b->dataOffset; });
        bodies.erase(std::unique(bodies.begin(), bodies.end(), [](auto* a, auto* b) { return a->dataOffset == b->dataOffset; }), bodies.end());
    }
    stats_.rigidBodyCount = static_cast<int>(bodies.size());

    for (auto* rb : bodies) {
        if (rb->shape.type != Hkx::ShapeType::Unknown) {
            size_t before = meshes_.size();
            buildMeshesFromShape(rb->shape, rb->motion.motionState.transform);
            collectShapeOffsets(rb->shape, renderedOffsets);
            for (size_t i = before; i < meshes_.size(); i++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "RB shape: %s (pos %.1f,%.1f,%.1f)", rb->shape.className.c_str(), rb->position.x, rb->position.y, rb->position.z);
                meshes_[i].label = buf;
            }
        }
    }

    Hkx::Transform standaloneTransform;
    if (result.rigidBodies.size() == 1) {
        auto& t = result.rigidBodies[0].motion.motionState.transform;
        if (std::abs(t.translation.x)>0.001f||std::abs(t.translation.y)>0.001f||std::abs(t.translation.z)>0.001f) standaloneTransform = t;
    }
    for (auto& shape : result.shapes) {
        if (renderedOffsets.count(shape.dataOffset)) continue;
        bool skip = false;
        for (auto* rb : bodies) { if (rb->shape.type==Hkx::ShapeType::Unknown) continue; if (shape.className==rb->shape.className){skip=true;break;} for(auto&child:rb->shape.children){if(shape.className==child.className&&shape.numVertices==child.numVertices&&shape.numTriangles==child.numTriangles){skip=true;break;}} if(skip)break; }
        if (skip) continue;
        size_t before = meshes_.size();
        buildMeshesFromShape(shape, standaloneTransform);
        for (size_t i = before; i < meshes_.size(); i++) meshes_[i].label = "Standalone: " + shape.className;
    }

    Hkx::Transform sceneRootTransform;
    for (const auto& scene : result.scenes) buildSceneMeshes(scene, sceneRootTransform);

    for (auto& m : meshes_) { stats_.totalVertices += static_cast<int>(m.vertices.size()/3); stats_.totalTriangles += static_cast<int>(m.indices.size()/3); }
    std::cerr << "[HKX] loadParseResult: " << meshes_.size() << " meshes, "
              << stats_.totalVertices << " verts, " << stats_.totalTriangles << " tris, "
              << stats_.rigidBodyCount << " bodies, " << stats_.shapeCount << " shapes" << std::endl;
    fitToVisible();
    emit statsChanged();
}

void HkxGLWidget::buildSceneMeshes(const Hkx::SceneInfo& scene, const Hkx::Transform& rootTransform) {
    if (scene.meshes.empty()) return;
    if (scene.rootNodeIndex >= 0 && scene.rootNodeIndex < static_cast<int>(scene.nodes.size())) {
        walkSceneNode(scene, scene.rootNodeIndex, rootTransform);
    } else {
        for (size_t mi = 0; mi < scene.meshes.size(); mi++) {
            const auto& mesh = scene.meshes[mi];
            if (mesh.vertices.empty() || mesh.triangles.empty()) continue;
            RenderMesh rm;
            rm.color[0]=0.7f;rm.color[1]=0.65f;rm.color[2]=0.6f;rm.color[3]=0.25f;
            rm.wireColor[0]=0.5f;rm.wireColor[1]=0.5f;rm.wireColor[2]=0.55f;
            rm.vertices.reserve(mesh.vertices.size()*3);
            for (const auto& v : mesh.vertices) { auto p=transformPoint(rootTransform,v.x,v.y,v.z); rm.vertices.push_back(p[0]);rm.vertices.push_back(p[1]);rm.vertices.push_back(p[2]); }
            for (const auto& tri : mesh.triangles) { if(tri.a>=mesh.vertices.size()||tri.b>=mesh.vertices.size()||tri.c>=mesh.vertices.size()) continue; rm.indices.push_back(tri.a);rm.indices.push_back(tri.b);rm.indices.push_back(tri.c); }
            char buf[128]; snprintf(buf,sizeof(buf),"Scene mesh %zu (%zu verts, %zu tris)",mi,mesh.vertices.size(),mesh.triangles.size()); rm.label=buf;
            stats_.sceneMeshCount++;stats_.sceneMeshVertices+=static_cast<int>(mesh.vertices.size());stats_.sceneMeshTriangles+=static_cast<int>(rm.indices.size()/3);
            meshInfo_.push_back({Hkx::ShapeType::Unknown,true}); meshes_.push_back(std::move(rm));
        }
    }
}

void HkxGLWidget::walkSceneNode(const Hkx::SceneInfo& scene, int nodeIndex, const Hkx::Transform& parentTransform) {
    if (nodeIndex<0||nodeIndex>=static_cast<int>(scene.nodes.size())) return;
    const auto& node = scene.nodes[nodeIndex];
    Hkx::Transform worldTransform = combineTransforms(parentTransform, node.transform);
    if (node.meshIndex>=0&&node.meshIndex<static_cast<int>(scene.meshes.size())) {
        const auto& mesh = scene.meshes[node.meshIndex];
        bool has_nonzero = false;
        for (const auto& v : mesh.vertices) { if(std::abs(v.x)>1e-6f||std::abs(v.y)>1e-6f||std::abs(v.z)>1e-6f){has_nonzero=true;break;} }
        if (has_nonzero&&!mesh.vertices.empty()&&!mesh.triangles.empty()) {
            RenderMesh rm;
            rm.color[0]=0.7f;rm.color[1]=0.65f;rm.color[2]=0.6f;rm.color[3]=0.25f;
            rm.wireColor[0]=0.5f;rm.wireColor[1]=0.5f;rm.wireColor[2]=0.55f;
            for (const auto& v : mesh.vertices) { auto p=transformPoint(worldTransform,v.x,v.y,v.z); rm.vertices.push_back(p[0]);rm.vertices.push_back(p[1]);rm.vertices.push_back(p[2]); }
            for (const auto& tri : mesh.triangles) { if(tri.a>=mesh.vertices.size()||tri.b>=mesh.vertices.size()||tri.c>=mesh.vertices.size()) continue; rm.indices.push_back(tri.a);rm.indices.push_back(tri.b);rm.indices.push_back(tri.c); }
            rm.label = "Scene node: " + node.name + " (mesh " + std::to_string(node.meshIndex) + ", " + std::to_string(mesh.vertices.size()) + " verts)";
            stats_.sceneMeshCount++;stats_.sceneMeshVertices+=static_cast<int>(mesh.vertices.size());stats_.sceneMeshTriangles+=static_cast<int>(rm.indices.size()/3);
            meshInfo_.push_back({Hkx::ShapeType::Unknown,true}); meshes_.push_back(std::move(rm));
        }
    }
    for (int childIdx : node.childIndices) walkSceneNode(scene, childIdx, worldTransform);
}

// --- Shape tessellation ---

void HkxGLWidget::buildMeshesFromShape(const Hkx::ShapeInfo& shape, const Hkx::Transform& parentTransform) {
    if (shape.type==Hkx::ShapeType::Unknown) return;
    stats_.shapeCount++;
    switch (shape.type) {
    case Hkx::ShapeType::Box: addBox(shape, parentTransform); break;
    case Hkx::ShapeType::Sphere: addSphere(shape, parentTransform); break;
    case Hkx::ShapeType::Capsule: addCapsule(shape, parentTransform); break;
    case Hkx::ShapeType::Cylinder: addCylinder(shape, parentTransform); break;
    case Hkx::ShapeType::ConvexVertices: addConvexVertices(shape, parentTransform); break;
    case Hkx::ShapeType::CompressedMesh: addCompressedMesh(shape, parentTransform); break;
    case Hkx::ShapeType::SimpleContainer: case Hkx::ShapeType::ExtendedMesh: addMeshVertices(shape, parentTransform); break;
    case Hkx::ShapeType::Triangle: addTriangleMesh(shape, parentTransform); break;
    case Hkx::ShapeType::Transform: case Hkx::ShapeType::ConvexTransform: { auto c=combineTransforms(parentTransform,shape.childTransform); for(auto&child:shape.children) buildMeshesFromShape(child,c); break; }
    case Hkx::ShapeType::ConvexTranslate: { Hkx::Transform ct; ct.translation=shape.translation; ct.translation.w=1.0f; auto c=combineTransforms(parentTransform,ct); for(auto&child:shape.children) buildMeshesFromShape(child,c); break; }
    case Hkx::ShapeType::Mopp: case Hkx::ShapeType::List: case Hkx::ShapeType::BvTree: for(auto&child:shape.children) buildMeshesFromShape(child,parentTransform); break;
    default: break;
    }
}

void HkxGLWidget::addBox(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform) {
    float hx=shape.halfExtents.x,hy=shape.halfExtents.y,hz=shape.halfExtents.z;
    std::array<std::array<float,3>,8> c;
    c[0]=transformPoint(xform,-hx,-hy,-hz);c[1]=transformPoint(xform,hx,-hy,-hz);c[2]=transformPoint(xform,hx,hy,-hz);c[3]=transformPoint(xform,-hx,hy,-hz);
    c[4]=transformPoint(xform,-hx,-hy,hz);c[5]=transformPoint(xform,hx,-hy,hz);c[6]=transformPoint(xform,hx,hy,hz);c[7]=transformPoint(xform,-hx,hy,hz);
    RenderMesh m; shapeColor(Hkx::ShapeType::Box,m.color,m.wireColor);
    for(auto&v:c){m.vertices.push_back(v[0]);m.vertices.push_back(v[1]);m.vertices.push_back(v[2]);}
    uint32_t idx[]={0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,2,6,7,2,7,3,0,3,7,0,7,4,1,5,6,1,6,2};
    m.indices.assign(idx,idx+36);meshInfo_.push_back({Hkx::ShapeType::Box,false});meshes_.push_back(std::move(m));
}

void HkxGLWidget::addSphere(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform) {
    auto c=transformPoint(xform,0,0,0);
    auto prim = lu::assets::generate_sphere(c[0], c[1], c[2], shape.radius, 16, 24);
    RenderMesh m; shapeColor(Hkx::ShapeType::Sphere,m.color,m.wireColor);
    m.vertices = std::move(prim.vertices); m.indices = std::move(prim.indices);
    meshInfo_.push_back({Hkx::ShapeType::Sphere,false});meshes_.push_back(std::move(m));
}

void HkxGLWidget::addCapsule(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform) {
    auto ta=transformPoint(xform,shape.vertexA.x,shape.vertexA.y,shape.vertexA.z);
    auto tb=transformPoint(xform,shape.vertexB.x,shape.vertexB.y,shape.vertexB.z);
    float a[3]={ta[0],ta[1],ta[2]}, b[3]={tb[0],tb[1],tb[2]};
    auto prim = lu::assets::generate_capsule(a, b, shape.radius, 12, 16);
    RenderMesh m; shapeColor(Hkx::ShapeType::Capsule,m.color,m.wireColor);
    m.vertices = std::move(prim.vertices); m.indices = std::move(prim.indices);
    meshInfo_.push_back({Hkx::ShapeType::Capsule,false});meshes_.push_back(std::move(m));
}

void HkxGLWidget::addCylinder(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform) {
    auto ta=transformPoint(xform,shape.vertexA.x,shape.vertexA.y,shape.vertexA.z);
    auto tb=transformPoint(xform,shape.vertexB.x,shape.vertexB.y,shape.vertexB.z);
    float radius=shape.cylRadius; if(radius<=0) radius=shape.radius;
    float a[3]={ta[0],ta[1],ta[2]}, b[3]={tb[0],tb[1],tb[2]};
    auto prim = lu::assets::generate_cylinder(a, b, radius, 16);
    if(prim.vertices.empty()) return;
    RenderMesh m; shapeColor(Hkx::ShapeType::Cylinder,m.color,m.wireColor);
    m.vertices = std::move(prim.vertices); m.indices = std::move(prim.indices);
    meshInfo_.push_back({Hkx::ShapeType::Cylinder,false});meshes_.push_back(std::move(m));
}

void HkxGLWidget::addConvexVertices(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform) {
    std::vector<std::array<float,3>> lv;
    for(auto&ftp:shape.rotatedVertices){float xs[4]={ftp.xs.x,ftp.xs.y,ftp.xs.z,ftp.xs.w},ys[4]={ftp.ys.x,ftp.ys.y,ftp.ys.z,ftp.ys.w},zs[4]={ftp.zs.x,ftp.zs.y,ftp.zs.z,ftp.zs.w};for(int v=0;v<4&&static_cast<int>(lv.size())<shape.numVertices;v++)lv.push_back({xs[v],ys[v],zs[v]});}
    int nv=shape.numVertices; if(nv<=0)nv=static_cast<int>(lv.size()); if(nv>static_cast<int>(lv.size()))nv=static_cast<int>(lv.size()); lv.resize(nv); if(lv.empty())return;
    std::vector<std::array<float,3>> wv; wv.reserve(lv.size()); for(auto&v:lv)wv.push_back(transformPoint(xform,v[0],v[1],v[2]));
    RenderMesh m; shapeColor(Hkx::ShapeType::ConvexVertices,m.color,m.wireColor);
    for(auto&v:wv){m.vertices.push_back(v[0]);m.vertices.push_back(v[1]);m.vertices.push_back(v[2]);}
    if(!shape.planeEquations.empty()){
        float tol=std::max(0.01f,shape.radius*2.0f+0.001f);
        for(const auto&pl:shape.planeEquations){
            std::vector<int> fi;for(size_t i=0;i<lv.size();i++){float d=pl.x*lv[i][0]+pl.y*lv[i][1]+pl.z*lv[i][2]+pl.w;if(std::abs(d)<=tol)fi.push_back(static_cast<int>(i));}
            if(fi.size()<3)continue;
            float ax,ay,az;if(std::abs(pl.x)<0.9f){ax=0;ay=-pl.z;az=pl.y;}else{ax=-pl.z;ay=0;az=pl.x;}float al=std::sqrt(ax*ax+ay*ay+az*az);if(al<1e-6f)continue;ax/=al;ay/=al;az/=al;
            float bx=pl.y*az-pl.z*ay,by=pl.z*ax-pl.x*az,bz=pl.x*ay-pl.y*ax;
            float cx=0,cy=0,cz=0;for(int i:fi){cx+=lv[i][0];cy+=lv[i][1];cz+=lv[i][2];}cx/=fi.size();cy/=fi.size();cz/=fi.size();
            std::vector<std::pair<float,int>>angles;for(int i:fi){float dx=lv[i][0]-cx,dy=lv[i][1]-cy,dz=lv[i][2]-cz;angles.push_back({std::atan2(dx*bx+dy*by+dz*bz,dx*ax+dy*ay+dz*az),i});}
            std::sort(angles.begin(),angles.end());uint32_t v0=angles[0].second;for(size_t i=1;i+1<angles.size();i++){m.indices.push_back(v0);m.indices.push_back(angles[i].second);m.indices.push_back(angles[i+1].second);}
        }
    }else if(nv>=3){for(int i=1;i<nv-1;i++){m.indices.push_back(0);m.indices.push_back(i);m.indices.push_back(i+1);}}
    if(!m.vertices.empty()&&!m.indices.empty()){meshInfo_.push_back({Hkx::ShapeType::ConvexVertices,false});meshes_.push_back(std::move(m));}
}

void HkxGLWidget::addMeshVertices(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform) {
    std::vector<std::array<float,3>> v;
    for(const auto&p:shape.planeEquations)v.push_back(transformPoint(xform,p.x,p.y,p.z));
    if(v.empty()&&!shape.rotatedVertices.empty())for(auto&ftp:shape.rotatedVertices){float xs[4]={ftp.xs.x,ftp.xs.y,ftp.xs.z,ftp.xs.w},ys[4]={ftp.ys.x,ftp.ys.y,ftp.ys.z,ftp.ys.w},zs[4]={ftp.zs.x,ftp.zs.y,ftp.zs.z,ftp.zs.w};for(int i=0;i<4;i++)v.push_back(transformPoint(xform,xs[i],ys[i],zs[i]));}
    if(v.empty()){for(auto&child:shape.children)buildMeshesFromShape(child,xform);return;}
    RenderMesh m;shapeColor(shape.type,m.color,m.wireColor);for(auto&p:v){m.vertices.push_back(p[0]);m.vertices.push_back(p[1]);m.vertices.push_back(p[2]);}
    for(auto&tri:shape.triangles){if(tri.a>=v.size()||tri.b>=v.size()||tri.c>=v.size())continue;m.indices.push_back(tri.a);m.indices.push_back(tri.b);m.indices.push_back(tri.c);}
    if(!m.vertices.empty()&&!m.indices.empty()){meshInfo_.push_back({shape.type,false});meshes_.push_back(std::move(m));}
    for(auto&child:shape.children)buildMeshesFromShape(child,xform);
}

void HkxGLWidget::addTriangleMesh(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform) {
    if(shape.triangles.empty())return;
    std::vector<std::array<float,3>> v;
    for(auto&ftp:shape.rotatedVertices){float xs[4]={ftp.xs.x,ftp.xs.y,ftp.xs.z,ftp.xs.w},ys[4]={ftp.ys.x,ftp.ys.y,ftp.ys.z,ftp.ys.w},zs[4]={ftp.zs.x,ftp.zs.y,ftp.zs.z,ftp.zs.w};for(int i=0;i<4;i++)v.push_back(transformPoint(xform,xs[i],ys[i],zs[i]));}
    if(v.empty())return;
    RenderMesh m;shapeColor(shape.type,m.color,m.wireColor);for(auto&p:v){m.vertices.push_back(p[0]);m.vertices.push_back(p[1]);m.vertices.push_back(p[2]);}
    for(auto&tri:shape.triangles){m.indices.push_back(tri.a);m.indices.push_back(tri.b);m.indices.push_back(tri.c);}
    if(!m.vertices.empty()&&!m.indices.empty()){meshInfo_.push_back({shape.type,false});meshes_.push_back(std::move(m));}
}

void HkxGLWidget::addCompressedMesh(const Hkx::ShapeInfo& shape, const Hkx::Transform& xform) {
    if(!shape.compressedMesh)return;auto&cm=*shape.compressedMesh;
    for(auto&chunk:cm.chunks){
        Hkx::Transform cx=xform;if(chunk.transformIndex!=0xFFFF&&chunk.transformIndex<cm.transforms.size())cx=combineTransforms(xform,cm.transforms[chunk.transformIndex]);
        RenderMesh m;shapeColor(Hkx::ShapeType::CompressedMesh,m.color,m.wireColor);
        int nv=static_cast<int>(chunk.vertices.size())/3;m.vertices.reserve(nv*3);
        for(int i=0;i<nv;i++){float lx=chunk.offset.x+chunk.vertices[i*3]*cm.error,ly=chunk.offset.y+chunk.vertices[i*3+1]*cm.error,lz=chunk.offset.z+chunk.vertices[i*3+2]*cm.error;auto w=transformPoint(cx,lx,ly,lz);m.vertices.push_back(w[0]);m.vertices.push_back(w[1]);m.vertices.push_back(w[2]);}
        for(auto idx:chunk.indices)m.indices.push_back(static_cast<uint32_t>(idx));
        if(!m.vertices.empty()&&!m.indices.empty()){meshInfo_.push_back({Hkx::ShapeType::CompressedMesh,false});meshes_.push_back(std::move(m));}
    }
    if(!cm.bigTriangles.empty()&&!cm.bigVertices.empty()){
        RenderMesh m;shapeColor(Hkx::ShapeType::CompressedMesh,m.color,m.wireColor);
        for(auto&v:cm.bigVertices){auto w=transformPoint(xform,v.x,v.y,v.z);m.vertices.push_back(w[0]);m.vertices.push_back(w[1]);m.vertices.push_back(w[2]);}
        for(auto&bt:cm.bigTriangles){m.indices.push_back(bt.a);m.indices.push_back(bt.b);m.indices.push_back(bt.c);}
        meshInfo_.push_back({Hkx::ShapeType::CompressedMesh,false});meshes_.push_back(std::move(m));
    }
    for(auto&piece:cm.convexPieces){
        Hkx::Transform px=xform;if(piece.transformIndex!=0xFFFF&&piece.transformIndex<cm.transforms.size())px=combineTransforms(xform,cm.transforms[piece.transformIndex]);
        RenderMesh m;m.color[0]=1;m.color[1]=0.6f;m.color[2]=0.2f;m.color[3]=0.35f;m.wireColor[0]=1;m.wireColor[1]=0.7f;m.wireColor[2]=0.3f;
        int nv=static_cast<int>(piece.vertices.size())/3;
        for(int i=0;i<nv;i++){float lx=piece.offset.x+piece.vertices[i*3]*cm.error,ly=piece.offset.y+piece.vertices[i*3+1]*cm.error,lz=piece.offset.z+piece.vertices[i*3+2]*cm.error;auto w=transformPoint(px,lx,ly,lz);m.vertices.push_back(w[0]);m.vertices.push_back(w[1]);m.vertices.push_back(w[2]);}
        if(!piece.faceOffsets.empty()&&!piece.faceVertices.empty()){for(size_t fi=0;fi<piece.faceOffsets.size();fi++){uint16_t s=piece.faceOffsets[fi],e=(fi+1<piece.faceOffsets.size())?piece.faceOffsets[fi+1]:static_cast<uint16_t>(piece.faceVertices.size());if(e-s>=3){uint32_t v0=piece.faceVertices[s];for(uint16_t j=s+1;j<e-1;j++){m.indices.push_back(v0);m.indices.push_back(piece.faceVertices[j]);m.indices.push_back(piece.faceVertices[j+1]);}}}}
        else if(nv>=3){for(int i=1;i<nv-1;i++){m.indices.push_back(0);m.indices.push_back(i);m.indices.push_back(i+1);}}
        if(!m.vertices.empty()&&!m.indices.empty()){meshInfo_.push_back({Hkx::ShapeType::CompressedMesh,false});meshes_.push_back(std::move(m));}
    }
}

void HkxGLWidget::shapeColor(Hkx::ShapeType type, float* rgba, float* wire) {
    switch(type){
    case Hkx::ShapeType::Box:rgba[0]=0.2f;rgba[1]=0.4f;rgba[2]=1.0f;rgba[3]=0.35f;wire[0]=0.4f;wire[1]=0.6f;wire[2]=1.0f;break;
    case Hkx::ShapeType::Sphere:rgba[0]=0.2f;rgba[1]=0.9f;rgba[2]=0.3f;rgba[3]=0.3f;wire[0]=0.3f;wire[1]=1.0f;wire[2]=0.4f;break;
    case Hkx::ShapeType::Capsule:case Hkx::ShapeType::Cylinder:rgba[0]=1.0f;rgba[1]=0.9f;rgba[2]=0.2f;rgba[3]=0.3f;wire[0]=1;wire[1]=1;wire[2]=0.4f;break;
    case Hkx::ShapeType::ConvexVertices:rgba[0]=0.2f;rgba[1]=0.8f;rgba[2]=0.9f;rgba[3]=0.3f;wire[0]=0.3f;wire[1]=0.9f;wire[2]=1;break;
    case Hkx::ShapeType::CompressedMesh:rgba[0]=1;rgba[1]=0.5f;rgba[2]=0.1f;rgba[3]=0.35f;wire[0]=1;wire[1]=0.6f;wire[2]=0.2f;break;
    case Hkx::ShapeType::ExtendedMesh:case Hkx::ShapeType::SimpleContainer:case Hkx::ShapeType::Triangle:rgba[0]=0.9f;rgba[1]=0.2f;rgba[2]=0.2f;rgba[3]=0.35f;wire[0]=1;wire[1]=0.3f;wire[2]=0.3f;break;
    default:rgba[0]=0.7f;rgba[1]=0.7f;rgba[2]=0.7f;rgba[3]=0.3f;wire[0]=0.9f;wire[1]=0.9f;wire[2]=0.9f;break;
    }
}

} // namespace hkx_viewer
