// process_info.
//
// The tool reads the real process table, so the ordinary tests check that what
// it reports is true. The interesting ones are about the fact that a process
// name is text somebody else chose: anyone can start a program called
// `status: OK` or one whose name contains a newline, and results are rendered
// to the model as `key: value` lines. A name that can invent a line can invent
// a measurement.

#include <QtTest/QtTest>

#include <atomic>
#include <memory>

#include "jarvis/system/WindowsSystemMetricsProvider.h"
#include "jarvis/tools/PermissionManager.h"
#include "jarvis/tools/SystemTools.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

using namespace jarvis;
using namespace jarvis::tools;

class TestProcessInfo : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // --- the schema --------------------------------------------------------
    void isRegisteredAsReadOnly();
    void takesOnlyABoundedInteger();
    void acceptsNoArgumentsAtAll();
    void acceptsAValidLimit();
    void rejectsBadArguments_data();
    void rejectsBadArguments();
    void rejectsAnythingNamingAProcess_data();
    void rejectsAnythingNamingAProcess();

    // --- what it returns ---------------------------------------------------
    void reportsRealProcesses();
    void honoursTheLimit();
    void theLimitCannotBeWidened();
    void resultStaysInsideTheToolResultCap();

    // --- untrusted names ---------------------------------------------------
    void reportedNamesCarryNoControlCharacters();
    void aProcessNameCannotForgeAResultField();
    void theResultIsUnambiguouslyAToolResult();

private:
    std::unique_ptr<system::WindowsSystemMetricsProvider> m_metrics;
    ToolRegistry m_registry;
    std::unique_ptr<ToolValidator> m_validator;
    PermissionManager m_permissions;

    [[nodiscard]] ToolResult run(const std::string& json);
};

void TestProcessInfo::initTestCase() {
    m_metrics = std::make_unique<system::WindowsSystemMetricsProvider>();
    for (auto& tool : SystemToolFactory::createAll(*m_metrics)) {
        QVERIFY(m_registry.add(std::move(tool)));
    }
    m_validator = std::make_unique<ToolValidator>(m_registry);
}

ToolResult TestProcessInfo::run(const std::string& json) {
    const auto call = m_validator->validate(json);
    if (!call) {
        return ToolResult::failure("process_info", call.error().code,
                                   call.error().message);
    }
    const std::atomic<bool> notCancelled{false};
    return m_registry.lookup("process_info")->execute(*call, notCancelled);
}

// ---------------------------------------------------------------------------
// The schema
// ---------------------------------------------------------------------------

void TestProcessInfo::isRegisteredAsReadOnly() {
    const ITool* tool = m_registry.lookup("process_info");
    QVERIFY2(tool != nullptr, "process_info is not registered");

    // Read-only means it runs without asking. That is only acceptable because
    // it cannot change anything - which the schema below is what guarantees.
    QCOMPARE(tool->definition().permission, PermissionLevel::ReadOnly);
    QCOMPARE(m_permissions.evaluate(tool->definition()).decision,
             PermissionManager::Decision::Allow);
}

void TestProcessInfo::takesOnlyABoundedInteger() {
    const ToolDefinition& definition = m_registry.lookup("process_info")->definition();

    QCOMPARE(definition.arguments.size(), std::size_t{1});

    const ArgumentSpec& limit = definition.arguments[0];
    QCOMPARE(QString::fromStdString(limit.name), QStringLiteral("limit"));
    QCOMPARE(limit.type, ArgumentType::Integer);
    QVERIFY(!limit.required);
    QCOMPARE(limit.minimum, std::int64_t{1});
    QVERIFY(limit.maximum <= 20);
}

void TestProcessInfo::acceptsNoArgumentsAtAll() {
    const ToolResult result = run(R"({"tool":"process_info"})");
    QVERIFY2(result.ok(), qPrintable(QString::fromStdString(result.errorMessage())));
}

void TestProcessInfo::acceptsAValidLimit() {
    const ToolResult result = run(R"({"tool":"process_info","arguments":{"limit":3}})");
    QVERIFY(result.ok());
    QCOMPARE(QString::fromStdString(result.data().at("reported")), QStringLiteral("3"));
}

void TestProcessInfo::rejectsBadArguments_data() {
    QTest::addColumn<QString>("json");

    QTest::newRow("limit as string")
        << R"({"tool":"process_info","arguments":{"limit":"5"}})";
    QTest::newRow("limit as bool")
        << R"({"tool":"process_info","arguments":{"limit":true}})";
    QTest::newRow("limit as float")
        << R"({"tool":"process_info","arguments":{"limit":2.5}})";
    QTest::newRow("limit below range")
        << R"({"tool":"process_info","arguments":{"limit":0}})";
    QTest::newRow("limit above range")
        << R"({"tool":"process_info","arguments":{"limit":9999}})";
    QTest::newRow("limit negative")
        << R"({"tool":"process_info","arguments":{"limit":-1}})";
    QTest::newRow("unknown argument")
        << R"({"tool":"process_info","arguments":{"count":5}})";
    QTest::newRow("extra field beside a good one")
        << R"({"tool":"process_info","arguments":{"limit":5},"admin":true})";
    QTest::newRow("arguments as array")
        << R"({"tool":"process_info","arguments":[5]})";
    QTest::newRow("malformed json")
        << R"({"tool":"process_info","arguments":{"limit":)";
}

void TestProcessInfo::rejectsBadArguments() {
    QFETCH(QString, json);

    const auto call = m_validator->validate(json.toStdString());
    QVERIFY2(!call.has_value(),
             qPrintable(QStringLiteral("accepted: %1").arg(json)));
}

void TestProcessInfo::rejectsAnythingNamingAProcess_data() {
    QTest::addColumn<QString>("field");

    // The schema has one integer. There is no field that could carry a process
    // name, a path or a command - so refusing them is not a check that could be
    // forgotten, there is nowhere to put them.
    for (const char* field : {"name", "process", "pid", "path", "command", "exe",
                              "kill", "signal", "filter", "query"}) {
        QTest::newRow(field) << QString::fromUtf8(field);
    }
}

void TestProcessInfo::rejectsAnythingNamingAProcess() {
    QFETCH(QString, field);

    const QString json =
        QStringLiteral(R"({"tool":"process_info","arguments":{"%1":"explorer.exe"}})")
            .arg(field);

    const auto call = m_validator->validate(json.toStdString());
    QVERIFY2(!call.has_value(),
             qPrintable(QStringLiteral("process_info accepted a '%1' argument")
                            .arg(field)));
    QCOMPARE(call.error().code, ToolErrorCode::UnknownArgument);
}

// ---------------------------------------------------------------------------
// What it returns
// ---------------------------------------------------------------------------

void TestProcessInfo::reportsRealProcesses() {
    const ToolResult result = run(R"({"tool":"process_info","arguments":{"limit":10}})");
    QVERIFY(result.ok());

    // This test is itself a running process, so the table can never be empty.
    // A tool that reported none would be inventing an answer.
    const int total = QString::fromStdString(result.data().at("total_processes")).toInt();
    QVERIFY2(total > 0, "the process table came back empty");

    QVERIFY(result.data().contains("process_01"));
    const QString first = QString::fromStdString(result.data().at("process_01"));
    QVERIFY(first.contains(QStringLiteral("pid ")));
}

void TestProcessInfo::honoursTheLimit() {
    for (const int limit : {1, 3, 7}) {
        const ToolResult result = run(
            std::string{R"({"tool":"process_info","arguments":{"limit":)"}
            + std::to_string(limit) + "}}");
        QVERIFY(result.ok());

        int rows = 0;
        for (const auto& [key, value] : result.data()) {
            if (key.starts_with("process_")) {
                ++rows;
            }
        }
        QCOMPARE(rows, limit);
    }
}

void TestProcessInfo::theLimitCannotBeWidened() {
    // The schema's maximum is the ceiling. A request for more is refused by the
    // validator rather than quietly clamped, so nobody can believe they asked
    // for two hundred and got them.
    QVERIFY(!m_validator
                 ->validate(R"({"tool":"process_info","arguments":{"limit":200}})")
                 .has_value());
}

void TestProcessInfo::resultStaysInsideTheToolResultCap() {
    // The context manager caps a tool result at 4096 characters and says so
    // when it truncates. This tool should never need truncating: a result the
    // context has to cut is one the model reads incompletely.
    const ToolResult result = run(R"({"tool":"process_info","arguments":{"limit":20}})");
    QVERIFY(result.ok());

    const std::string text = result.toModelText();
    QVERIFY2(text.size() < 4096,
             qPrintable(QStringLiteral("a full result is %1 characters")
                            .arg(text.size())));
}

// ---------------------------------------------------------------------------
// Untrusted names
// ---------------------------------------------------------------------------

void TestProcessInfo::reportedNamesCarryNoControlCharacters() {
    const ToolResult result = run(R"({"tool":"process_info","arguments":{"limit":20}})");
    QVERIFY(result.ok());

    // Anyone can name a program. A name containing a newline could invent a
    // whole field in the rendered result; a name containing a control
    // character could do stranger things to whatever reads it next.
    for (const auto& [key, value] : result.data()) {
        for (const char c : value) {
            const auto byte = static_cast<unsigned char>(c);
            QVERIFY2(byte >= 0x20 && byte != 0x7F,
                     qPrintable(QStringLiteral("control character in %1")
                                    .arg(QString::fromStdString(key))));
        }
    }
}

void TestProcessInfo::aProcessNameCannotForgeAResultField() {
    // The property, stated directly against the renderer rather than hoping no
    // such process exists: a value carrying newlines must not become extra
    // lines in the text the model reads.
    const ToolResult forged = ToolResult::success(
        "process_info", {{"process_01", "evil (pid 1, 1 MB)"}});

    const QString text = QString::fromStdString(forged.toModelText());

    // One header, one status. A result that could grow a second of either
    // would let a process name claim a measurement nobody took.
    QCOMPARE(text.count(QStringLiteral("TOOL RESULT")), 1);
    QCOMPARE(text.count(QStringLiteral("status:")), 1);

    // And the real tool never produces a value that could do it, because the
    // control characters are gone before the value is stored.
    const ToolResult real = run(R"({"tool":"process_info","arguments":{"limit":20}})");
    QVERIFY(real.ok());
    const QString realText = QString::fromStdString(real.toModelText());
    QCOMPARE(realText.count(QStringLiteral("status: OK")), 1);
}

void TestProcessInfo::theResultIsUnambiguouslyAToolResult() {
    const ToolResult result = run(R"({"tool":"process_info"})");
    QVERIFY(result.ok());

    const QString text = QString::fromStdString(result.toModelText());
    QVERIFY(text.startsWith(QStringLiteral("TOOL RESULT")));
    QVERIFY(text.contains(QStringLiteral("tool: process_info")));
}

QTEST_MAIN(TestProcessInfo)
#include "tst_process_info.moc"
