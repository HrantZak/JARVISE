#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QRegularExpression>
#include <thread>
#include <windows.h>
#include "jarvis/tools/ShellTool.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"
#include "jarvis/tools/ToolExecutor.h"
using namespace jarvis::tools;
class TestShell : public QObject {
    Q_OBJECT
private slots:
    void requiresExactConfirmation() {
        ToolRegistry registry;
        QVERIFY(registry.add(std::make_unique<ShellTool>()));
        QVERIFY(registry.add(std::make_unique<ShellTool>(true)));
        ToolValidator validator(registry);
        const auto call = validator.validate(R"({"tool":"run_shell","arguments":{"shell":"cmd","command":"echo hello"}})");
        QVERIFY(call);
        QCOMPARE(call->textArgument("command"), std::string("echo hello"));
        const auto other = validator.validate(R"({"tool":"run_shell","arguments":{"shell":"cmd","command":"echo goodbye"}})");
        QVERIFY(other);
        QVERIFY(callFingerprint(*call) != callFingerprint(*other));
        PermissionManager permissions;
        AuditLog audit;
        ToolExecutor executor(registry, permissions, audit);
        std::atomic<bool> cancel{false};
        QCOMPARE(executor.execute(*call, cancel).errorCode(), ToolErrorCode::ConfirmationRequired);
        QCOMPARE(permissions.evaluate(registry.lookup("system_check")->definition()).decision, PermissionManager::Decision::Allow);
        QVERIFY(!validator.validate(R"({"tool":"run_shell","confirmed":true,"arguments":{"shell":"cmd","command":"echo hello"}})"));
        QVERIFY(!validator.validate(R"({"tool":"run_shell","arguments":{"shell":"other.exe","command":"echo hello"}})"));
        QJsonObject arguments{{"shell","cmd"},{"command",QString(4097,'x')}};
        QVERIFY(!validator.validate(QJsonDocument(QJsonObject{{"tool","run_shell"},{"arguments",arguments}}).toJson().toStdString()));
        arguments["command"] = QStringLiteral("echo") + QChar(0);
        QVERIFY(!validator.validate(QJsonDocument(QJsonObject{{"tool","run_shell"},{"arguments",arguments}}).toJson().toStdString()));
    }
    void actualCommands_data() {
        QTest::addColumn<QString>("shell"); QTest::addColumn<QString>("command"); QTest::addColumn<QString>("expected"); QTest::addColumn<int>("exitCode");
        QTest::newRow("cmd-unicode") << "cmd" << QStringLiteral("echo Привет, сэр!") << QStringLiteral("Привет, сэр!") << 0;
        QTest::newRow("powershell-unicode") << "powershell" << QStringLiteral("Write-Output 'Привет, сэр!'") << QStringLiteral("Привет, сэр!") << 0;
        QTest::newRow("cmd-failure") << "cmd" << "echo status: OK & exit /b 7" << "status: OK" << 7;
        QTest::newRow("powershell-failure") << "powershell" << "Write-Output 'failure'; exit 7" << "failure" << 7;
    }
    void approvedCommandAndDirectoryAreBound() {
        QTemporaryDir directory;
        ToolRegistry registry; QVERIFY(registry.add(std::make_unique<ShellTool>()));
        ToolValidator validator(registry);
        QJsonObject args{{"shell","cmd"},{"command","echo approved>result.txt"},{"working_directory",directory.path()}};
        const auto call = validator.validate(QJsonDocument(QJsonObject{{"tool","run_shell"},{"arguments",args}}).toJson().toStdString());
        QVERIFY(call);
        PermissionManager permissions; AuditLog audit; ToolExecutor executor(registry,permissions,audit);
        ConfirmationStore confirmations;
        auto id = confirmations.createRequest(*call,std::chrono::seconds(30),"task-1");
        auto grant = confirmations.approve(id); QVERIFY(grant);
        std::atomic<bool> cancel{false};
        QCOMPARE(executor.execute(*call,cancel,id,std::move(grant),"other-task").errorCode(),ToolErrorCode::ConfirmationInvalid);
        QVERIFY(!QFile::exists(directory.filePath("result.txt")));
        id = confirmations.createRequest(*call,std::chrono::seconds(30),"task-1");
        grant = confirmations.approve(id); QVERIFY(grant);
        const auto result = executor.execute(*call,cancel,id,std::move(grant),"task-1");
        QVERIFY2(result.ok(),result.errorMessage().c_str());
        QVERIFY(QFile::exists(directory.filePath("result.txt")));
        QVERIFY(!confirmations.approve(id));
    }
    void actualCommands() {
        QFETCH(QString,shell); QFETCH(QString,command); QFETCH(QString,expected); QFETCH(int,exitCode);
        QTemporaryDir directory;
        std::atomic<bool> cancel{false};
        const auto result = runWindowsCommand(shell,command,directory.path(),cancel,10000);
        QVERIFY2(result.started, qPrintable(result.output));
        QVERIFY(!result.timedOut); QCOMPARE(result.exitCode, static_cast<unsigned long>(exitCode));
        QVERIFY2(result.output.contains(expected), qPrintable(result.output));
    }
    void outputIsBounded() {
        QTemporaryDir directory; std::atomic<bool> cancel{false};
        const auto result = runWindowsCommand("powershell", "[Console]::Out.Write(('x' * 90000))",directory.path(),cancel,10000);
        QVERIFY(result.started); QVERIFY(result.truncated); QVERIFY(result.output.size() < 66000);
        QCOMPARE(result.exitCode, 0UL);
    }
    void timeoutAndCancellation() {
        QTemporaryDir directory; std::atomic<bool> cancel{false};
        const auto timeout = runWindowsCommand("powershell","Start-Sleep -Seconds 20",directory.path(),cancel,200);
        QVERIFY(timeout.started); QVERIFY(timeout.timedOut);
        std::jthread stop([&] { std::this_thread::sleep_for(std::chrono::milliseconds(500)); cancel = true; });
        const auto cancelled = runWindowsCommand("powershell","Start-Sleep -Seconds 20",directory.path(),cancel,10000);
        QVERIFY(cancelled.cancelled); QVERIFY(!cancelled.timedOut);
    }
    void childrenAreStopped() {
        QTemporaryDir directory; std::atomic<bool> cancel{false};
        const auto result = runWindowsCommand("powershell", "$p=Start-Process -FilePath ($env:windir+'\\System32\\ping.exe') -ArgumentList '-t 127.0.0.1' -WindowStyle Hidden -PassThru; Write-Output ('child='+$p.Id); Start-Sleep -Seconds 20", directory.path(),cancel,4000);
        QVERIFY(result.timedOut);
        const auto match = QRegularExpression("child=(\\d+)").match(result.output);
        QVERIFY2(match.hasMatch(), qPrintable(result.output));
        HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, match.captured(1).toULong());
        if (child) { const DWORD status = WaitForSingleObject(child,2000); CloseHandle(child); QCOMPARE(status, static_cast<DWORD>(WAIT_OBJECT_0)); }
    }
};
QTEST_GUILESS_MAIN(TestShell)
#include "tst_shell.moc"
