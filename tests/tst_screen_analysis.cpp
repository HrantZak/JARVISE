#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include <QFile>
#include <QFileInfo>

#include "ScreenCommand.h"
#include "RecordingTool.h"
#include "jarvis/tools/ScreenAnalysisTool.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

#include <atomic>
#include <memory>
#include <future>

using namespace jarvis;

class TestScreenAnalysis : public QObject {
    Q_OBJECT

private slots:
    void recognisesExplicitLookRequests();
    void choosesForegroundWindowForConcreteTarget();
    void ignoresOrdinaryQuestionsAndOptOuts();
    void isReadOnlyWithClosedTargetSchema();
    void recordingStopWithoutSessionDoesNotToggle() {
        tools::ToolRegistry registry;
        registry.add(std::make_unique<app::RecordingTool>());
        tools::ToolValidator validator(registry);
        auto call = validator.validate(R"({"tool":"windows_recording","arguments":{"action":"stop"}})");
        QVERIFY(call.has_value());
        app::RecordingTool tool;
        std::atomic<bool> cancelled{false};
        QVERIFY(!tool.execute(*call, cancelled).ok());
        auto start = validator.validate(R"({"tool":"windows_recording","arguments":{"action":"start"}})");
        QVERIFY(start.has_value());
        cancelled.store(true);
        QVERIFY(!tool.execute(*start, cancelled).ok());
        QVERIFY(!validator.validate(R"({"tool":"nvidia_recording","arguments":{"action":"start"}})").has_value());
    }
    void recordingCommands() {
        for (const auto& pair : {std::pair{QStringLiteral("начни запись экрана"), QStringLiteral("start")}, std::pair{QStringLiteral("останови запись"), QStringLiteral("stop")}}) {
            const auto call = QJsonDocument::fromJson(app::resolveScreenCommand(pair.first).toUtf8()).object();
            QCOMPARE(call["tool"].toString(), QStringLiteral("windows_recording"));
            QCOMPARE(call["arguments"].toObject()["action"].toString(), pair.second);
        }
        QVERIFY(app::resolveScreenCommand(QStringLiteral("не включай запись экрана")).isEmpty());
    }
    void screenshotCommand() {
        QCOMPARE(QJsonDocument::fromJson(app::resolveScreenCommand(QStringLiteral("сделай скриншот")).toUtf8()).object()["tool"].toString(), QStringLiteral("take_screenshot"));
        QVERIFY(app::resolveScreenCommand(QStringLiteral("не делай скриншот")).isEmpty());
        tools::ScreenAnalysisTool tool(true);
        QCOMPARE(tool.definition().permission, tools::PermissionLevel::SafeAction);
    }
    void recognizesTextWithoutTemporaryFile();
};

void TestScreenAnalysis::recognisesExplicitLookRequests() {
    for (const QString& request : {
             QStringLiteral("смотри на экран"),
             QStringLiteral("проанализируй что видно на экране"),
             QStringLiteral("реши пример, который виден на экране")}) {
        const QString call = app::resolveScreenCommand(request);
        QVERIFY2(!call.isEmpty(), qPrintable(request));
        QVERIFY(call.contains(QStringLiteral("screen_analyze")));
    }
}

void TestScreenAnalysis::choosesForegroundWindowForConcreteTarget() {
    const QString call = app::resolveScreenCommand(
        QStringLiteral("посмотри на код в этом окне"));
    QVERIFY(call.contains(QStringLiteral("active_window")));
}

void TestScreenAnalysis::ignoresOrdinaryQuestionsAndOptOuts() {
    QVERIFY(app::resolveScreenCommand(QStringLiteral("что ты умеешь")).isEmpty());
    QVERIFY(app::resolveScreenCommand(QStringLiteral("не смотри на экран")).isEmpty());
    QVERIFY(app::resolveScreenCommand(QStringLiteral("открой файл screen.cpp")).isEmpty());
}

void TestScreenAnalysis::isReadOnlyWithClosedTargetSchema() {
    tools::ScreenAnalysisTool tool;
    QCOMPARE(QString::fromStdString(tool.definition().name),
             QStringLiteral("screen_analyze"));
    QCOMPARE(tool.definition().permission, tools::PermissionLevel::ReadOnly);
    QCOMPARE(tool.definition().arguments.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(tool.definition().arguments.front().name),
             QStringLiteral("target"));
    QCOMPARE(tool.definition().arguments.front().allowedValues.size(), std::size_t{2});
}

void TestScreenAnalysis::recognizesTextWithoutTemporaryFile() {
    QImage image(900, 180, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::black);
    QFont font("Arial", 40);
    painter.setFont(font);
    painter.drawText(30, 110, "JARVIS 123 + 456");
    painter.end();
    QString status;
    const QString text = std::async(std::launch::async, [&] { return tools::ScreenAnalysisTool::recognizeImage(image, status); }).get();
    QVERIFY2(text.contains("123") && text.contains("456"), qPrintable(status + ": " + text));
}
QTEST_MAIN(TestScreenAnalysis)
#include "tst_screen_analysis.moc"
