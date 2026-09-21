#include "Mesh.h"
#include "Expression.h"
#include <QColor>
#include <algorithm>
#include <functional>
#include <numbers>

namespace jarvis::scene3d {
namespace {
constexpr float pi=std::numbers::pi_v<float>;
struct Builder {
    Mesh mesh;
    quint32 vertex(QVector3D p, QVector3D n, float u=0, float v=0, QVector3D color={1,1,1}) {
        mesh.vertices.push_back({p.x(),p.y(),p.z(),n.x(),n.y(),n.z(),u,v,color.x(),color.y(),color.z(),1});
        return static_cast<quint32>(mesh.vertices.size()-1);
    }
    void triangle(QVector3D a,QVector3D b,QVector3D c,QVector3D color={1,1,1}) {
        const auto n=QVector3D::crossProduct(b-a,c-a).normalized();
        const auto first=vertex(a,n,0,0,color); vertex(b,n,1,0,color); vertex(c,n,1,1,color);
        mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2});
    }
    void box(QVector3D center,QVector3D size,QVector3D color={1,1,1}) {
        const float x=size.x()/2,y=size.y()/2,z=size.z()/2;
        const std::array<QVector3D,8> p={center+QVector3D{-x,-y,-z},center+QVector3D{x,-y,-z},center+QVector3D{x,y,-z},center+QVector3D{-x,y,-z},
            center+QVector3D{-x,-y,z},center+QVector3D{x,-y,z},center+QVector3D{x,y,z},center+QVector3D{-x,y,z}};
        const int faces[6][4]={{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{3,7,6,2},{0,1,5,4}};
        for(const auto& f:faces) { triangle(p[f[0]],p[f[1]],p[f[2]],color); triangle(p[f[0]],p[f[2]],p[f[3]],color); }
    }
    void grid(int nu,int nv,const std::function<QVector3D(float,float)>& f,bool reverse=false) {
        const auto base=static_cast<quint32>(mesh.vertices.size());
        std::vector<bool> valid;
        for(int j=0;j<=nv;++j) for(int i=0;i<=nu;++i) {
            const float u=static_cast<float>(i)/nu,v=static_cast<float>(j)/nv;
            auto p=f(u,v);
            const bool good=std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z()) && p.length()<100;
            valid.push_back(good);
            if(!good) p={0,0,0};
            auto normal=QVector3D::crossProduct(f(u,v+0.0001F)-f(u,v-0.0001F),f(u+0.0001F,v)-f(u-0.0001F,v)).normalized();
            if(!std::isfinite(normal.x()) || !std::isfinite(normal.y()) || !std::isfinite(normal.z()) || normal.lengthSquared()<0.1F) normal={0,1,0};
            if(reverse) normal=-normal;
            vertex(p,normal,u,v);
        }
        for(int j=0;j<nv;++j) for(int i=0;i<nu;++i) {
            const quint32 a=static_cast<quint32>(j*(nu+1)+i),b=a+1,c=a+static_cast<quint32>(nu+1),d=c+1;
            if(valid[a] && valid[c] && valid[b]) {
                if(reverse) mesh.indices.insert(mesh.indices.end(),{base+a,base+b,base+c});
                else mesh.indices.insert(mesh.indices.end(),{base+a,base+c,base+b});
            }
            if(valid[b] && valid[c] && valid[d]) {
                if(reverse) mesh.indices.insert(mesh.indices.end(),{base+b,base+d,base+c});
                else mesh.indices.insert(mesh.indices.end(),{base+b,base+c,base+d});
            }
        }
    }
};
}
Mesh generateMesh(const QVariantMap& spec) {
    Builder b;
    try {
        const auto kind=spec.value("kind").toString();
        const int n=std::clamp(spec.value("resolution",48).toInt(),8,700);
        if(kind=="cube") b.box({0,0,0},{1.6F,1.6F,1.6F});
        else if(kind=="house") {
            b.box({0,-0.15F,0},{2,1.5F,1.6F},{0.9F,0.88F,0.8F});
            const QVector3D a{-1.15F,0.6F,-0.95F}, c{1.15F,0.6F,-0.95F}, d{0,1.4F,-0.95F};
            const QVector3D e{-1.15F,0.6F,0.95F}, f{1.15F,0.6F,0.95F}, g{0,1.4F,0.95F}, roof{0.48F,0.18F,0.1F};
            b.triangle(a,d,c,roof); b.triangle(e,f,g,roof);
            b.triangle(a,e,g,roof); b.triangle(a,g,d,roof); b.triangle(d,g,f,roof); b.triangle(d,f,c,roof);
            b.box({0,-0.45F,0.815F},{0.4F,0.9F,0.05F},{0.25F,0.14F,0.09F});
            for(float x : {-0.64F,0.64F}) b.box({x,0.1F,0.82F},{0.38F,0.42F,0.06F},{0.25F,0.7F,0.95F});
            for(float z : {-0.42F,0.42F}) b.box({1.015F,0.1F,z},{0.05F,0.42F,0.38F},{0.25F,0.7F,0.95F});
            b.box({0,-0.96F,0},{2.3F,0.12F,1.9F},{0.42F,0.44F,0.46F});
        } else if(kind=="sphere") b.grid(n,n,[](float u,float v){ const float t=pi*v,p=2*pi*u; return QVector3D{std::sin(t)*std::cos(p),std::cos(t),std::sin(t)*std::sin(p)}; },true);
        else if(kind=="torus") b.grid(n,n/2,[](float u,float v){const float a=2*pi*u,t=2*pi*v,r=0.75F+0.28F*std::cos(t);return QVector3D{r*std::cos(a),0.28F*std::sin(t),r*std::sin(a)};});
        else if(kind=="cylinder" || kind=="mug") {
            const bool mug=kind=="mug";
            b.grid(n,1,[](float u,float v){return QVector3D{0.7F*std::cos(2*pi*u),-0.8F+1.6F*v,0.7F*std::sin(2*pi*u)};});
            b.grid(n,1,[](float u,float v){return QVector3D{0.7F*v*std::cos(2*pi*u),-0.8F,0.7F*v*std::sin(2*pi*u)};},true);
            if(!mug) b.grid(n,1,[](float u,float v){return QVector3D{0.7F*v*std::cos(2*pi*u),0.8F,0.7F*v*std::sin(2*pi*u)};});
            else {
                b.grid(n,1,[](float u,float v){return QVector3D{0.58F*std::cos(2*pi*u),-0.67F+1.47F*v,0.58F*std::sin(2*pi*u)};},true);
                b.grid(n,1,[](float u,float v){const float r=0.58F+0.12F*v;return QVector3D{r*std::cos(2*pi*u),0.8F,r*std::sin(2*pi*u)};});
                b.grid(n,1,[](float u,float v){return QVector3D{0.58F*v*std::cos(2*pi*u),-0.67F,0.58F*v*std::sin(2*pi*u)};});
                b.grid(n,n/2,[](float u,float v){const float a=2*pi*u,t=2*pi*v,r=0.52F+0.12F*std::cos(t);return QVector3D{0.83F+r*std::cos(a),r*std::sin(a),0.12F*std::sin(t)};});
            }
        } else if(kind=="surface") {
            Expression formula(spec.value("expression","sin(x)*cos(y)").toString());
            b.grid(n,n,[&](float u,float v){const double x=(u-0.5)*6,y=(v-0.5)*6;return QVector3D{static_cast<float>(x*0.55),static_cast<float>(formula.value(x,y)*0.55),static_cast<float>(y*0.55)};});
        } else throw std::runtime_error("Unknown model");
        if(b.mesh.indices.empty()) throw std::runtime_error("No finite surface in the selected domain");
        const float sx=std::clamp(spec.value("width",1).toFloat(),0.1F,5.0F),sy=std::clamp(spec.value("height",1).toFloat(),0.1F,5.0F),sz=std::clamp(spec.value("depth",1).toFloat(),0.1F,5.0F);
        b.mesh.minimum={100,100,100}; b.mesh.maximum={-100,-100,-100};
        for(auto& vertex:b.mesh.vertices) {
            vertex.x*=sx; vertex.y*=sy; vertex.z*=sz;
            const auto normal=QVector3D{vertex.nx/sx,vertex.ny/sy,vertex.nz/sz}.normalized();
            vertex.nx=normal.x();vertex.ny=normal.y();vertex.nz=normal.z();
            if(spec.value("texture").toString()=="checker") {
                const float tint=(static_cast<int>(vertex.u*12)+static_cast<int>(vertex.v*12))%2 ? 0.5F:1.0F;
                vertex.r*=tint;vertex.g*=tint;vertex.b*=tint;
            }
            b.mesh.minimum.setX(std::min(b.mesh.minimum.x(),vertex.x)); b.mesh.maximum.setX(std::max(b.mesh.maximum.x(),vertex.x));
            b.mesh.minimum.setY(std::min(b.mesh.minimum.y(),vertex.y)); b.mesh.maximum.setY(std::max(b.mesh.maximum.y(),vertex.y));
            b.mesh.minimum.setZ(std::min(b.mesh.minimum.z(),vertex.z)); b.mesh.maximum.setZ(std::max(b.mesh.maximum.z(),vertex.z));
        }
    } catch(const std::exception& error) { b.mesh={}; b.mesh.error=QString::fromUtf8(error.what()); }
    return std::move(b.mesh);
}
}
