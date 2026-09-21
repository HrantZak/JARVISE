#pragma once
#include <QVariantMap>
#include <QVector3D>
#include <array>
#include <vector>

namespace jarvis::scene3d {
struct Vertex { float x,y,z,nx,ny,nz,u,v,r,g,b,a; };
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<quint32> indices;
    QVector3D minimum, maximum;
    QString error;
};
Mesh generateMesh(const QVariantMap& spec);
}
