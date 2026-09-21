#include "SceneGeometry.h"
#include <QtConcurrent/QtConcurrentRun>

namespace jarvis::scene3d {
SceneGeometry::SceneGeometry(QQuick3DObject* parent):QQuick3DGeometry(parent) {
    connect(&m_watcher,&QFutureWatcher<Mesh>::finished,this,[this] {
        m_busy=false;
        if(m_running!=m_spec) { start(); return; }
        const auto mesh=m_watcher.result();
        clear(); m_error=mesh.error; m_triangles=static_cast<int>(mesh.indices.size()/3);
        if(m_error.isEmpty()) {
            setStride(sizeof(Vertex)); setPrimitiveType(PrimitiveType::Triangles);
            setVertexData(QByteArray(reinterpret_cast<const char*>(mesh.vertices.data()), static_cast<qsizetype>(mesh.vertices.size()*sizeof(Vertex))));
            setIndexData(QByteArray(reinterpret_cast<const char*>(mesh.indices.data()), static_cast<qsizetype>(mesh.indices.size()*sizeof(quint32))));
            addAttribute(Attribute::PositionSemantic,0,Attribute::F32Type);
            addAttribute(Attribute::NormalSemantic,3*sizeof(float),Attribute::F32Type);
            addAttribute(Attribute::TexCoordSemantic,6*sizeof(float),Attribute::F32Type);
            addAttribute(Attribute::ColorSemantic,8*sizeof(float),Attribute::F32Type);
            addAttribute(Attribute::IndexSemantic,0,Attribute::U32Type);
            setBounds(mesh.minimum,mesh.maximum);
        }
        update(); emit readyChanged();
    });
}
void SceneGeometry::setSpec(const QVariantMap& spec) {
    QVariantMap geometry;
    for(const auto* key:{"kind","expression","resolution","width","height","depth","texture"})
        if(spec.contains(key)) geometry.insert(key,spec.value(key));
    if(m_spec==geometry) return;
    m_spec=geometry; emit specChanged();
    if(!m_busy) start();
}
void SceneGeometry::start() {
    if(m_spec.isEmpty()) return;
    m_busy=true; m_running=m_spec; m_error.clear(); emit readyChanged();
    const auto snapshot=m_spec;
    m_watcher.setFuture(QtConcurrent::run([snapshot]{return generateMesh(snapshot);}));
}
}
