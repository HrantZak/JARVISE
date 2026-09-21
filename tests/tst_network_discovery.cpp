#include <QtTest/QtTest>

#include "NetworkCommand.h"
#include "jarvis/tools/NetworkDiscoveryTool.h"

using namespace jarvis;

class TestNetworkDiscovery : public QObject {
    Q_OBJECT

private slots:
    void recognisesCommonRussianRequests();
    void ignoresUnrelatedRequests();
    void isReadOnlyAndArgumentFree();
};

void TestNetworkDiscovery::recognisesCommonRussianRequests() {
    for (const QString& request : {
             QStringLiteral("кто подключен к моей сети"),
             QStringLiteral("сколько телефонов подключено к вайфай"),
             QStringLiteral("покажи устройства в локальной сети")}) {
        const QString call = app::resolveNetworkDiscoveryCommand(request);
        QVERIFY2(!call.isEmpty(), qPrintable(request));
        QVERIFY(call.contains(QStringLiteral("network_discovery")));
    }
}

void TestNetworkDiscovery::ignoresUnrelatedRequests() {
    QVERIFY(app::resolveNetworkDiscoveryCommand(QStringLiteral("открой музыку"))
            .isEmpty());
    QVERIFY(app::resolveNetworkDiscoveryCommand(QStringLiteral("кто ты"))
            .isEmpty());
}

void TestNetworkDiscovery::isReadOnlyAndArgumentFree() {
    tools::NetworkDiscoveryTool tool;
    QCOMPARE(QString::fromStdString(tool.definition().name),
             QStringLiteral("network_discovery"));
    QCOMPARE(tool.definition().permission, tools::PermissionLevel::ReadOnly);
    QVERIFY(tool.definition().arguments.empty());
}

QTEST_MAIN(TestNetworkDiscovery)
#include "tst_network_discovery.moc"
