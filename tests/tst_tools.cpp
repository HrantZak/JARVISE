// Security tests for the tool boundary.
//
// These tests treat model output as hostile input. They are not here to show
// that the happy path works - that is the smallest part of the file - but to
// pin down what happens when the input is crafted to get through. Every attack
// listed in the Phase 5 specification has a case here, and each one asserts a
// specific refusal rather than merely "not a success", so a change that starts
// refusing for the wrong reason is still caught.
//
// The rule these tests exist to defend: SECURITY MUST NOT DEPEND ON MODEL
// BEHAVIOUR. Nothing below asks the model to behave. Every assertion holds
// against output written specifically to break the parser.

#include <QtTest/QtTest>

#include <atomic>
#include <memory>
#include <string>

#include "jarvis/system/WindowsSystemMetricsProvider.h"
#include "jarvis/tools/PermissionManager.h"
#include "jarvis/tools/SystemTools.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

using namespace jarvis::tools;

namespace {

/// A tool with one argument of each kind, so argument validation can be tested
/// without depending on what the real tools happen to accept today.
class FixtureTool final : public ITool {
public:
    FixtureTool(std::string name, PermissionLevel permission)
        : m_definition{makeDefinition(std::move(name), permission)} {}

    [[nodiscard]] const ToolDefinition& definition() const override {
        return m_definition;
    }

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& cancelled) override {
        ++executions;
        if (cancelled.load()) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Cancelled,
                                       "cancelled");
        }
        return ToolResult::success(call.toolName(), {{"ok", "true"}});
    }

    /// Counts real executions, so a test can assert that a refused call did not
    /// merely fail afterwards but never reached the tool at all.
    int executions{0};

private:
    ToolDefinition m_definition;

    static ToolDefinition makeDefinition(std::string name, PermissionLevel permission) {
        ToolDefinition definition;
        definition.name = std::move(name);
        definition.description = "fixture";
        definition.permission = permission;

        ArgumentSpec level;
        level.name = "level";
        level.type = ArgumentType::Integer;
        level.required = false;
        level.minimum = 0;
        level.maximum = 100;
        definition.arguments.push_back(std::move(level));

        ArgumentSpec verbose;
        verbose.name = "verbose";
        verbose.type = ArgumentType::Boolean;
        verbose.required = false;
        definition.arguments.push_back(std::move(verbose));

        ArgumentSpec mode;
        mode.name = "mode";
        mode.type = ArgumentType::Enumeration;
        mode.required = false;
        mode.allowedValues = {"fast", "slow"};
        definition.arguments.push_back(std::move(mode));

        return definition;
    }
};

} // namespace

class TestTools : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // --- registry ---------------------------------------------------------
    void registryRejectsDuplicateNames();
    void registryLookupIsExactOnly_data();
    void registryLookupIsExactOnly();

    // --- validator: accepted input ---------------------------------------
    void acceptsMinimalCall();
    void acceptsEveryArgumentType();
    void extractsCallFromSurroundingProse();

    // --- validator: malformed input --------------------------------------
    void rejectsEmptyInput();
    void rejectsMalformedJson();
    void rejectsNonObject();
    void rejectsMissingToolField();
    void rejectsNonStringToolField();
    void rejectsOversizedPayload();
    void rejectsInvalidUtf8();

    // --- validator: the attacks ------------------------------------------
    void rejectsUnknownTool();
    void rejectsUnknownArgument();
    void rejectsExtraTopLevelField_data();
    void rejectsExtraTopLevelField();
    void rejectsWrongArgumentType_data();
    void rejectsWrongArgumentType();
    void rejectsOutOfRangeInteger_data();
    void rejectsOutOfRangeInteger();
    void rejectsValueOutsideEnumeration_data();
    void rejectsValueOutsideEnumeration();
    void rejectsShellPayloadInEveryPosition_data();
    void rejectsShellPayloadInEveryPosition();
    void rejectsPathTraversalAsToolName();
    void rejectsCaseAlteredToolName();

    // --- permissions ------------------------------------------------------
    void deniedLevelSurvivesEveryPolicy();
    void confirmRequiredIsNeverAutoAllowed();
    void masterSwitchStopsEverything();

    // --- real tools -------------------------------------------------------
    void openApplicationHasNoPathArgument();
    void openApplicationAllowlistIsClosed_data();
    void openApplicationAllowlistIsClosed();
    void readOnlyToolsAreDeclaredReadOnly();
    void readOnlyToolsReturnRealMeasurements();
    void systemInfoMatchesTheProviderDirectly();

private:
    ToolRegistry m_registry;
    std::unique_ptr<ToolValidator> m_validator;
    FixtureTool* m_readTool{nullptr};

    /// Asserts that \p json is refused with exactly \p expected.
    void expectRefusal(const std::string& json, ToolErrorCode expected,
                       const char* what) const {
        const auto result = m_validator->validate(json);
        QVERIFY2(!result.has_value(),
                 qPrintable(QStringLiteral("accepted a call it must refuse: %1")
                                .arg(QString::fromStdString(what))));
        QCOMPARE(QString::fromUtf8(toolErrorName(result.error().code).data()),
                 QString::fromUtf8(toolErrorName(expected).data()));
    }
};

void TestTools::initTestCase() {
    auto readTool = std::make_unique<FixtureTool>("read_thing", PermissionLevel::ReadOnly);
    m_readTool = readTool.get();

    QVERIFY(m_registry.add(std::move(readTool)));
    QVERIFY(m_registry.add(
        std::make_unique<FixtureTool>("confirm_thing", PermissionLevel::ConfirmRequired)));
    QVERIFY(m_registry.add(
        std::make_unique<FixtureTool>("denied_thing", PermissionLevel::Denied)));

    m_validator = std::make_unique<ToolValidator>(m_registry);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

void TestTools::registryRejectsDuplicateNames() {
    ToolRegistry registry;
    QVERIFY(registry.add(std::make_unique<FixtureTool>("thing", PermissionLevel::ReadOnly)));

    // A second registration under the same name would let a later, laxer
    // definition shadow an earlier one. Names are identities.
    QVERIFY(!registry.add(
        std::make_unique<FixtureTool>("thing", PermissionLevel::ConfirmRequired)));
    QCOMPARE(registry.size(), std::size_t{1});
    QCOMPARE(registry.lookup("thing")->definition().permission, PermissionLevel::ReadOnly);
}

void TestTools::registryLookupIsExactOnly_data() {
    QTest::addColumn<QString>("name");

    QTest::newRow("trailing space") << "read_thing ";
    QTest::newRow("leading space") << " read_thing";
    QTest::newRow("uppercase") << "READ_THING";
    QTest::newRow("mixed case") << "Read_Thing";
    QTest::newRow("traversal prefix") << "../read_thing";
    QTest::newRow("namespace prefix") << "tools.read_thing";
    QTest::newRow("suffix") << "read_thing2";
    QTest::newRow("null-ish") << "read_thing\\0";
    QTest::newRow("substring") << "read";
}

void TestTools::registryLookupIsExactOnly() {
    QFETCH(QString, name);
    QVERIFY(!m_registry.contains(name.toStdString()));
}

// ---------------------------------------------------------------------------
// Validator - accepted input
// ---------------------------------------------------------------------------

void TestTools::acceptsMinimalCall() {
    const auto result = m_validator->validate(R"({"tool":"read_thing"})");
    QVERIFY(result.has_value());
    QCOMPARE(QString::fromStdString(result->toolName()), QStringLiteral("read_thing"));
}

void TestTools::acceptsEveryArgumentType() {
    const auto result = m_validator->validate(
        R"({"tool":"read_thing","arguments":{"level":42,"verbose":true,"mode":"fast"}})");
    QVERIFY(result.has_value());
    QCOMPARE(result->integerArgument("level", -1), std::int64_t{42});
    QCOMPARE(result->booleanArgument("verbose", false), true);
    QCOMPARE(QString::fromStdString(result->enumerationArgument("mode")),
             QStringLiteral("fast"));
}

void TestTools::extractsCallFromSurroundingProse() {
    // Models wrap calls in explanation and code fences. Finding the JSON is a
    // convenience, not a security decision - whatever comes out still goes
    // through validate() before anything happens.
    const std::string output =
        "Sure, let me check that for you.\n"
        "```json\n"
        R"({"tool":"read_thing","arguments":{"level":1}})"
        "\n```\n"
        "I will report back shortly.";

    const std::string json = ToolValidator::extractCallJson(output);
    QVERIFY(!json.empty());
    QVERIFY(m_validator->validate(json).has_value());

    QVERIFY(ToolValidator::extractCallJson("just a normal sentence").empty());
}

// ---------------------------------------------------------------------------
// Validator - malformed input
// ---------------------------------------------------------------------------

void TestTools::rejectsEmptyInput() {
    expectRefusal("", ToolErrorCode::MalformedJson, "empty input");
    expectRefusal("   \n\t ", ToolErrorCode::MalformedJson, "whitespace only");
}

void TestTools::rejectsMalformedJson() {
    expectRefusal(R"({"tool":"read_thing")", ToolErrorCode::MalformedJson, "truncated");
    expectRefusal(R"({tool: read_thing})", ToolErrorCode::MalformedJson, "unquoted");
    expectRefusal(R"({"tool":"read_thing",})", ToolErrorCode::MalformedJson, "trailing comma");
    expectRefusal("not json at all", ToolErrorCode::MalformedJson, "prose");
}

void TestTools::rejectsNonObject() {
    // An array parses fine and is then refused for its shape.
    expectRefusal(R"(["read_thing"])", ToolErrorCode::NotAnObject, "array");

    // Top-level scalars never reach the shape check: Qt's parser requires an
    // object or an array at the top level, so they are refused as malformed.
    // Which of the two codes comes back does not matter to security - what
    // matters is that no scalar can produce a ValidatedCall.
    for (const char* scalar : {R"("read_thing")", "42", "null", "true"}) {
        const auto result = m_validator->validate(scalar);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().code == ToolErrorCode::NotAnObject
                || result.error().code == ToolErrorCode::MalformedJson);
    }
}

void TestTools::rejectsMissingToolField() {
    expectRefusal(R"({"arguments":{"level":1}})", ToolErrorCode::MissingToolField,
                  "arguments without a tool");
    expectRefusal("{}", ToolErrorCode::MissingToolField, "empty object");
}

void TestTools::rejectsNonStringToolField() {
    expectRefusal(R"({"tool":42})", ToolErrorCode::MissingToolField, "numeric tool");
    expectRefusal(R"({"tool":null})", ToolErrorCode::MissingToolField, "null tool");
    expectRefusal(R"({"tool":["read_thing"]})", ToolErrorCode::MissingToolField,
                  "array tool");
    expectRefusal(R"({"tool":{"name":"read_thing"}})", ToolErrorCode::MissingToolField,
                  "object tool");
}

void TestTools::rejectsOversizedPayload() {
    // A model looping on output must not be able to make the parser allocate
    // without bound. The limit is checked before parsing, not after.
    std::string huge = R"({"tool":"read_thing","arguments":{"mode":")";
    huge.append(ToolValidator::kMaxCallLength + 1024, 'a');
    huge += R"("}})";

    expectRefusal(huge, ToolErrorCode::MalformedJson, "oversized payload");
}

void TestTools::rejectsInvalidUtf8() {
    // Raw bytes that are not valid UTF-8. Accepting them would mean the string
    // the validator compares is not the string a later layer sees.
    const std::string lonelyContinuation =
        std::string(R"({"tool":"read_thing","arguments":{"mode":")") + '\x80' + R"("}})";
    expectRefusal(lonelyContinuation, ToolErrorCode::MalformedJson,
                  "lone continuation byte");

    const std::string truncatedSequence =
        std::string(R"({"tool":"read_thing","arguments":{"mode":")") + '\xD0' + R"("}})";
    expectRefusal(truncatedSequence, ToolErrorCode::MalformedJson,
                  "truncated multi-byte sequence");
}

// ---------------------------------------------------------------------------
// Validator - the attacks
// ---------------------------------------------------------------------------

void TestTools::rejectsUnknownTool() {
    expectRefusal(R"({"tool":"run_shell"})", ToolErrorCode::UnknownTool, "invented tool");
    expectRefusal(R"({"tool":"delete_file","arguments":{"path":"C:\\Windows"}})",
                  ToolErrorCode::UnknownTool, "invented destructive tool");
    expectRefusal(R"({"tool":"register_tool","arguments":{"name":"shell"}})",
                  ToolErrorCode::UnknownTool, "self-registration");
    expectRefusal(R"({"tool":""})", ToolErrorCode::UnknownTool, "empty name");
}

void TestTools::rejectsUnknownArgument() {
    // An argument that is not in the schema invalidates the whole call. It is
    // never silently dropped: dropping it would mean the call that runs is not
    // the call that was checked.
    expectRefusal(R"({"tool":"read_thing","arguments":{"path":"C:\\Windows\\System32"}})",
                  ToolErrorCode::UnknownArgument, "smuggled path");
    expectRefusal(R"({"tool":"read_thing","arguments":{"level":1,"command":"whoami"}})",
                  ToolErrorCode::UnknownArgument, "extra argument beside a valid one");
}

void TestTools::rejectsExtraTopLevelField_data() {
    QTest::addColumn<QString>("json");

    // Each of these is an attempt to grant something by asserting it in the
    // payload. None of them is refused by a check for that specific word - the
    // rule is that only "tool" and "arguments" exist, so any other key is fatal
    // and a new attack word needs no new code.
    QTest::newRow("confirmed") << R"({"tool":"confirm_thing","confirmed":true})";
    QTest::newRow("permission") << R"({"tool":"confirm_thing","permission":"READ_ONLY"})";
    QTest::newRow("user_approved") << R"({"tool":"confirm_thing","user_approved":true})";
    QTest::newRow("skip_confirmation")
        << R"({"tool":"confirm_thing","skip_confirmation":true})";
    QTest::newRow("level") << R"({"tool":"denied_thing","level":"READ_ONLY"})";
    QTest::newRow("admin") << R"({"tool":"read_thing","admin":true})";
    QTest::newRow("system") << R"({"tool":"read_thing","system":"override"})";
    QTest::newRow("request_id") << R"({"tool":"confirm_thing","request_id":"1"})";
    QTest::newRow("audit") << R"({"tool":"read_thing","audit":false})";
    QTest::newRow("timeout") << R"({"tool":"read_thing","timeout_ms":9999999})";
    QTest::newRow("comment") << R"({"tool":"read_thing","//":"ignore this"})";
}

void TestTools::rejectsExtraTopLevelField() {
    QFETCH(QString, json);
    expectRefusal(json.toStdString(), ToolErrorCode::UnknownArgument,
                  "extra top-level field");
}

void TestTools::rejectsWrongArgumentType_data() {
    QTest::addColumn<QString>("json");

    QTest::newRow("string for integer")
        << R"({"tool":"read_thing","arguments":{"level":"42"}})";
    QTest::newRow("bool for integer")
        << R"({"tool":"read_thing","arguments":{"level":true}})";
    QTest::newRow("float for integer")
        << R"({"tool":"read_thing","arguments":{"level":1.5}})";
    QTest::newRow("null for integer")
        << R"({"tool":"read_thing","arguments":{"level":null}})";
    QTest::newRow("array for integer")
        << R"({"tool":"read_thing","arguments":{"level":[1]}})";
    QTest::newRow("object for integer")
        << R"({"tool":"read_thing","arguments":{"level":{"v":1}}})";
    QTest::newRow("string for bool")
        << R"({"tool":"read_thing","arguments":{"verbose":"true"}})";
    QTest::newRow("integer for bool")
        << R"({"tool":"read_thing","arguments":{"verbose":1}})";
    QTest::newRow("integer for enum")
        << R"({"tool":"read_thing","arguments":{"mode":0}})";
    QTest::newRow("array for enum")
        << R"({"tool":"read_thing","arguments":{"mode":["fast"]}})";
    QTest::newRow("arguments as array")
        << R"({"tool":"read_thing","arguments":[1,2]})";
    QTest::newRow("arguments as string")
        << R"({"tool":"read_thing","arguments":"level=1"})";
}

void TestTools::rejectsWrongArgumentType() {
    QFETCH(QString, json);
    const auto result = m_validator->validate(json.toStdString());
    QVERIFY(!result.has_value());
    // Either the argument type or the shape of "arguments" itself is wrong;
    // both are type failures and both refuse the call.
    QVERIFY(result.error().code == ToolErrorCode::WrongArgumentType
            || result.error().code == ToolErrorCode::ArgumentsNotAnObject);
}

void TestTools::rejectsOutOfRangeInteger_data() {
    QTest::addColumn<QString>("json");

    QTest::newRow("below minimum") << R"({"tool":"read_thing","arguments":{"level":-1}})";
    QTest::newRow("above maximum") << R"({"tool":"read_thing","arguments":{"level":101}})";
    QTest::newRow("far above") << R"({"tool":"read_thing","arguments":{"level":999999}})";
    QTest::newRow("very negative")
        << R"({"tool":"read_thing","arguments":{"level":-999999}})";
}

void TestTools::rejectsOutOfRangeInteger() {
    QFETCH(QString, json);
    expectRefusal(json.toStdString(), ToolErrorCode::ValueOutOfRange, "out of range");
}

void TestTools::rejectsValueOutsideEnumeration_data() {
    QTest::addColumn<QString>("json");

    QTest::newRow("unknown value") << R"({"tool":"read_thing","arguments":{"mode":"turbo"}})";
    QTest::newRow("empty value") << R"({"tool":"read_thing","arguments":{"mode":""}})";
    QTest::newRow("case altered") << R"({"tool":"read_thing","arguments":{"mode":"FAST"}})";
    QTest::newRow("padded") << R"({"tool":"read_thing","arguments":{"mode":"fast "}})";
    QTest::newRow("concatenated")
        << R"({"tool":"read_thing","arguments":{"mode":"fast,slow"}})";
}

void TestTools::rejectsValueOutsideEnumeration() {
    QFETCH(QString, json);
    expectRefusal(json.toStdString(), ToolErrorCode::ValueNotAllowed, "value not allowed");
}

void TestTools::rejectsShellPayloadInEveryPosition_data() {
    QTest::addColumn<QString>("json");

    // The payload is the same in each row; only where it is placed changes.
    // None of these is refused because it looks like a shell command - there is
    // no blocklist. They are refused because no field of any type accepts a
    // free-form string in the first place.
    QTest::newRow("as tool name")
        << R"({"tool":"cmd.exe /c del C:\\Windows\\System32"})";
    QTest::newRow("as enum value")
        << R"({"tool":"read_thing","arguments":{"mode":"fast; rm -rf /"}})";
    QTest::newRow("as unknown argument")
        << R"({"tool":"read_thing","arguments":{"command":"powershell -enc AAAA"}})";
    QTest::newRow("as extra top-level field")
        << R"({"tool":"read_thing","exec":"cmd.exe"})";
    QTest::newRow("as nested object")
        << R"({"tool":"read_thing","arguments":{"level":{"$exec":"cmd.exe"}}})";
    QTest::newRow("as tool name with traversal")
        << R"({"tool":"../../../Windows/System32/cmd.exe"})";
}

void TestTools::rejectsShellPayloadInEveryPosition() {
    QFETCH(QString, json);
    const auto result = m_validator->validate(json.toStdString());
    QVERIFY2(!result.has_value(), "a shell payload reached a ValidatedCall");
}

void TestTools::rejectsPathTraversalAsToolName() {
    expectRefusal(R"({"tool":"../read_thing"})", ToolErrorCode::UnknownTool, "traversal");
    expectRefusal(R"({"tool":"tools/read_thing"})", ToolErrorCode::UnknownTool, "path");
    expectRefusal(R"({"tool":"read_thing\\u0000"})", ToolErrorCode::UnknownTool,
                  "escaped null");
}

void TestTools::rejectsCaseAlteredToolName() {
    expectRefusal(R"({"tool":"READ_THING"})", ToolErrorCode::UnknownTool, "uppercase");
    expectRefusal(R"({"tool":"Read_Thing"})", ToolErrorCode::UnknownTool, "mixed case");
    expectRefusal(R"({"tool":" read_thing"})", ToolErrorCode::UnknownTool, "padded");
}

// ---------------------------------------------------------------------------
// Permissions
// ---------------------------------------------------------------------------

void TestTools::deniedLevelSurvivesEveryPolicy() {
    // Every combination of the four flags, including all-on. Denied means
    // denied: there is no policy that turns it into anything else.
    for (int mask = 0; mask < 16; ++mask) {
        PermissionManager::Policy policy;
        policy.toolsEnabled = (mask & 1) != 0;
        policy.allowReadOnly = (mask & 2) != 0;
        policy.allowSafeActions = (mask & 4) != 0;
        policy.allowConfirmedActions = (mask & 8) != 0;

        const PermissionManager manager{policy};
        const auto verdict = manager.evaluate(PermissionLevel::Denied);
        QCOMPARE(verdict.decision, PermissionManager::Decision::Deny);
    }
}

void TestTools::confirmRequiredIsNeverAutoAllowed() {
    PermissionManager::Policy policy;
    policy.toolsEnabled = true;
    policy.allowReadOnly = true;
    policy.allowSafeActions = true;
    policy.allowConfirmedActions = true;

    const PermissionManager permissive{policy};
    QCOMPARE(permissive.evaluate(PermissionLevel::ConfirmRequired).decision,
             PermissionManager::Decision::RequireConfirmation);

    // Turning the flag off refuses outright rather than prompting. There is no
    // setting anywhere that makes a CONFIRM_REQUIRED tool run unattended.
    policy.allowConfirmedActions = false;
    const PermissionManager restrictive{policy};
    QCOMPARE(restrictive.evaluate(PermissionLevel::ConfirmRequired).decision,
             PermissionManager::Decision::Deny);
}

void TestTools::masterSwitchStopsEverything() {
    PermissionManager::Policy policy;
    policy.toolsEnabled = false;
    policy.allowReadOnly = true;
    policy.allowSafeActions = true;
    policy.allowConfirmedActions = true;

    const PermissionManager manager{policy};
    for (const PermissionLevel level :
         {PermissionLevel::ReadOnly, PermissionLevel::SafeAction,
          PermissionLevel::ConfirmRequired, PermissionLevel::Denied}) {
        QCOMPARE(manager.evaluate(level).decision, PermissionManager::Decision::Deny);
    }
}

// ---------------------------------------------------------------------------
// Real tools
// ---------------------------------------------------------------------------

void TestTools::openApplicationHasNoPathArgument() {
    const OpenApplicationTool tool;
    const ToolDefinition& definition = tool.definition();

    QCOMPARE(definition.permission, PermissionLevel::SafeAction);

    // The schema must not contain a field that could carry a path, a command
    // line or any other free text. This is checked structurally: every argument
    // is an enumeration with a closed set of values.
    for (const ArgumentSpec& argument : definition.arguments) {
        QCOMPARE(argument.type, ArgumentType::Enumeration);
        QVERIFY(!argument.allowedValues.empty());
    }

    for (const char* forbidden : {"path", "command", "arguments", "exe", "file", "url"}) {
        QVERIFY2(definition.findArgument(forbidden) == nullptr,
                 qPrintable(QStringLiteral("open_application accepts a '%1' argument")
                                .arg(QString::fromUtf8(forbidden))));
    }
}

void TestTools::openApplicationAllowlistIsClosed_data() {
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("allowed");

    QTest::newRow("calculator") << "calculator" << true;
    QTest::newRow("notepad") << "notepad" << true;
    QTest::newRow("explorer") << "explorer" << true;
    QTest::newRow("settings") << "settings" << true;

    QTest::newRow("cmd") << "cmd" << false;
    QTest::newRow("cmd.exe") << "cmd.exe" << false;
    QTest::newRow("powershell") << "powershell" << false;
    QTest::newRow("regedit") << "regedit" << false;
    QTest::newRow("absolute path") << "C:\\Windows\\System32\\cmd.exe" << false;
    QTest::newRow("traversal") << "../../cmd.exe" << false;
    QTest::newRow("uppercase") << "NOTEPAD" << false;
    QTest::newRow("padded") << "notepad " << false;
    QTest::newRow("chained") << "notepad & cmd.exe" << false;
    QTest::newRow("empty") << "" << false;
}

void TestTools::openApplicationAllowlistIsClosed() {
    QFETCH(QString, value);
    QFETCH(bool, allowed);

    QCOMPARE(OpenApplicationTool::resolve(value.toStdString()).has_value(), allowed);
}

void TestTools::readOnlyToolsAreDeclaredReadOnly() {
    // The permission manager trusts a tool's declared level, so a tool that can
    // only read but is declared otherwise - or worse, one that acts but claims
    // READ_ONLY - is a real hole. Assert the declaration for every one of them.
    jarvis::system::WindowsSystemMetricsProvider provider;
    const auto tools = SystemToolFactory::createAll(provider);

    QVERIFY(!tools.empty());
    for (const auto& tool : tools) {
        const ToolDefinition& definition = tool->definition();
        QVERIFY2(definition.permission == PermissionLevel::ReadOnly,
                 qPrintable(QStringLiteral("%1 is not declared READ_ONLY")
                                .arg(QString::fromStdString(definition.name))));

        // Whatever they take is closed. Most take nothing at all; process_info
        // takes a bounded count. What none of them may ever take is an open
        // value - a name, a path, a query - because that is the only kind of
        // argument something could be smuggled through.
        //
        // Stated this way rather than as "no arguments" because the earlier
        // form was a fact about the tools that happened to exist, and this is
        // the property that actually keeps them safe.
        for (const ArgumentSpec& argument : definition.arguments) {
            switch (argument.type) {
            case ArgumentType::Text:
                QFAIL("Read-only system tools must not accept executable text");
                break;
            case ArgumentType::Integer:
                QVERIFY2(argument.minimum < argument.maximum,
                         qPrintable(QStringLiteral("%1.%2 is an unbounded integer")
                                        .arg(QString::fromStdString(definition.name),
                                             QString::fromStdString(argument.name))));
                break;
            case ArgumentType::Enumeration:
                QVERIFY2(!argument.allowedValues.empty(),
                         qPrintable(QStringLiteral("%1.%2 is an open enumeration")
                                        .arg(QString::fromStdString(definition.name),
                                             QString::fromStdString(argument.name))));
                break;
            case ArgumentType::Boolean:
                break;
            }
        }
        QVERIFY(!definition.description.empty());
    }
}

void TestTools::readOnlyToolsReturnRealMeasurements() {
    // Whatever these return has to come from the machine. A tool that cannot
    // read something must fail rather than produce a plausible number, so this
    // test accepts either a real reading or an explicit UNAVAILABLE - and
    // nothing in between.
    jarvis::system::WindowsSystemMetricsProvider provider;
    ToolRegistry registry;
    for (auto& tool : SystemToolFactory::createAll(provider)) {
        QVERIFY(registry.add(std::move(tool)));
    }

    const ToolValidator validator{registry};
    const std::atomic<bool> notCancelled{false};

    for (const ToolDefinition* definition : registry.definitions()) {
        const std::string json = R"({"tool":")" + definition->name + R"("})";
        const auto call = validator.validate(json);
        QVERIFY(call.has_value());

        const ToolResult result =
            registry.lookup(definition->name)->execute(*call, notCancelled);

        if (!result.ok()) {
            QCOMPARE(result.errorCode(), ToolErrorCode::Unavailable);
            continue;
        }

        QVERIFY2(!result.data().empty(),
                 qPrintable(QStringLiteral("%1 succeeded with no data")
                                .arg(QString::fromStdString(definition->name))));

        const QString text = QString::fromStdString(result.toModelText());
        QVERIFY(text.contains(QStringLiteral("status: OK")));
    }
}

void TestTools::systemInfoMatchesTheProviderDirectly() {
    // The tool must report what the monitor reports. If the two ever disagree,
    // one of them is inventing a number.
    jarvis::system::WindowsSystemMetricsProvider provider;
    ToolRegistry registry;
    for (auto& tool : SystemToolFactory::createAll(provider)) {
        QVERIFY(registry.add(std::move(tool)));
    }

    const ToolValidator validator{registry};
    const std::atomic<bool> notCancelled{false};

    const auto call = validator.validate(R"({"tool":"system_info"})");
    QVERIFY(call.has_value());

    const ToolResult result = registry.lookup("system_info")->execute(*call, notCancelled);
    QVERIFY(result.ok());

    const jarvis::system::HardwareProfile& profile = provider.profile();
    QCOMPARE(QString::fromStdString(result.data().at("cpu")),
             QString::fromStdString(profile.cpuName));

    qInfo().noquote() << QString::fromStdString(result.toModelText());
}

QTEST_MAIN(TestTools)
#include "tst_tools.moc"
