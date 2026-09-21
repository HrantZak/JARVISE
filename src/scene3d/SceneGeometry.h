#pragma once
#include <QQuick3DGeometry>
#include <QFutureWatcher>
#include "Mesh.h"

namespace jarvis::scene3d {
class SceneGeometry : public QQuick3DGeometry {
    Q_OBJECT
    Q_PROPERTY(QVariantMap spec READ spec WRITE setSpec NOTIFY specChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY readyChanged)
    Q_PROPERTY(int triangles READ triangles NOTIFY readyChanged)
    Q_PROPERTY(QString error READ error NOTIFY readyChanged)
public:
    explicit SceneGeometry(QQuick3DObject* parent=nullptr);
    QVariantMap spec() const { return m_spec; }
    void setSpec(const QVariantMap& spec);
    bool busy() const { return m_busy; }
    int triangles() const { return m_triangles; }
    QString error() const { return m_error; }
signals:
    void specChanged();
    void readyChanged();
private:
    void start();
    QVariantMap m_spec,m_running;
    QFutureWatcher<Mesh> m_watcher;
    bool m_busy=false;
    int m_triangles=0;
    QString m_error;
};
}
