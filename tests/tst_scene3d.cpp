#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "Mesh.h"
#include "Expression.h"
#include "SceneController.h"
using namespace jarvis::scene3d;
class SceneTest : public QObject {
    Q_OBJECT
private slots:
    void drawings() {
        SceneController c;
        QVERIFY(c.handle(QStringLiteral("объясни как работает видеокарта")));
        QCOMPARE(c.drawingLabels().size(),4);
        QVERIFY(c.applyDrawing("{\"title\":\"Text\",\"labels\":[{\"text\":\"Hello\",\"x\":0,\"y\":0,\"at\":0.5}]}"));
        QVERIFY(!c.applyDrawing("{\"title\":\"Text\",\"labels\":[{\"text\":\"Hello\",\"x\":0,\"y\":0,\"at\":2}]}"));
        for(const auto& command:{QStringLiteral("нарисуй дом"),QStringLiteral("нарисуй джойстик"),QStringLiteral("нарисуй линии синуса")}) {
            QVERIFY(c.handle(command)); QVERIFY(c.drawingMode()); QVERIFY(!c.strokes().isEmpty());
        }
        QVERIFY(!c.handle(QStringLiteral("нарисуй дом с башней и садом")));
        const auto previous=c.strokes();
        QVERIFY(!c.applyDrawing("{\"title\":\"bad\",\"paths\":[{\"points\":[[0,0],[99999,0]]}]}"));
        QCOMPARE(c.strokes(),previous);
        QVERIFY(c.applyDrawing("{\"title\":\"custom\",\"paths\":[{\"points\":[[0,0,0],[1,2,3],[2,0,1]]}]}"));
        QCOMPARE(c.drawingTitle(),QString("custom"));
    }
    void formula() {
        QCOMPARE(Expression("-2^2+3*4").value(0,0),8.0);
        QVERIFY(std::abs(Expression("sin(x)*cos(y)").value(1,2)-std::sin(1.0)*std::cos(2.0))<1e-10);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Expression("system('cmd')"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Expression(QString(300,'x')));
    }
    void meshes() {
        for(const auto* kind:{"cube","house","mug","sphere","cylinder","torus","surface"}) {
            const auto mesh=generateMesh({{"kind",kind},{"expression","sin(x)*cos(y)"},{"resolution",32}});
            QVERIFY2(mesh.error.isEmpty(),qPrintable(mesh.error));
            QVERIFY(!mesh.indices.empty());
            for(auto index:mesh.indices) QVERIFY(index<mesh.vertices.size());
            for(const auto& v:mesh.vertices) QVERIFY(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.nx));
        }
    }
    void largeSurface() {
        QElapsedTimer timer; timer.start();
        const auto mesh=generateMesh({{"kind","surface"},{"expression","sin(x)*cos(y)"},{"resolution",700}});
        QVERIFY(mesh.error.isEmpty());
        QCOMPARE(mesh.indices.size()/3,std::size_t(980000));
        qInfo()<<"980000 triangle generation, milliseconds:"<<timer.elapsed();
    }
    void commandsAndExport() {
        SceneController c;
        QVERIFY(!c.handle(QStringLiteral("открой браузер")));
        QVERIFY(c.handle(QStringLiteral("Джарвис, покажи куб")));
        QCOMPARE(c.items().size(),1);
        QCOMPARE(c.items()[0].toMap()["kind"].toString(),QString("cube"));
        c.handle(QStringLiteral("добавь дом"));
        QCOMPARE(c.items().size(),2);
        c.handle(QStringLiteral("останови модель"));
        QVERIFY(!c.rotating());
        c.handle(QStringLiteral("покажи график z = sin(x)*cos(y)"));
        QCOMPARE(c.items().size(),1);
        QCOMPARE(c.items()[0].toMap()["kind"].toString(),QString("surface"));
        QVERIFY(c.setParameter("resolution",700));
        c.handle(QStringLiteral("добавь сферу"));
        QVERIFY(!c.setParameter("resolution",700));
        c.handle(QStringLiteral("покажи дом"));
        QTemporaryDir dir; QVERIFY(dir.isValid()); c.exportDirectory=dir.path();
        QFile obj(c.exportScene("obj")); QVERIFY(obj.open(QIODevice::ReadOnly)); QVERIFY(obj.readAll().contains("\nf "));
        QFile gltf(c.exportScene("gltf")); QVERIFY(gltf.open(QIODevice::ReadOnly));
        const auto doc=QJsonDocument::fromJson(gltf.readAll()).object();
        QCOMPARE(doc["asset"].toObject()["version"].toString(),QString("2.0"));
        QVERIFY(!doc["meshes"].toArray().isEmpty());
        const auto buffer=doc["buffers"].toArray()[0].toObject();
        const auto bytes=QByteArray::fromBase64(buffer["uri"].toString().section(',',1).toLatin1());
        QCOMPARE(bytes.size(),buffer["byteLength"].toInt());
        const auto previous=c.lastExport();
        c.requestExport("gltf");
        QTRY_VERIFY_WITH_TIMEOUT(c.lastExport()!=previous,10000);
        QVERIFY(QFile::exists(c.lastExport()));
    }
};
QTEST_MAIN(SceneTest)
#include "tst_scene3d.moc"
