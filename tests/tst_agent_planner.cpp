// The planner.
//
// A plan is untrusted input with several tool calls in it instead of one. These
// tests treat it that way: every attack the single-call path faces is tried
// again here, plus the ones a plan makes possible - a good step hiding a bad
// one, a plan longer than the limit, a denied tool smuggled into the middle.
//
// The property that matters most: one invalid step rejects the whole plan.
// Skipping it would run a plan nobody proposed; running the prefix would leave
// the machine part-way through a task that was never coherent.

#include <QtTest/QtTest>

#include <memory>
#include <string>

#include "jarvis/agent/Planner.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

using namespace jarvis;
using namespace jarvis::agent;

namespace {

class FixtureTool final : public tools::ITool {
public:
    FixtureTool(std::string name, tools::PermissionLevel permission)
        : m_definition{makeDefinition(std::move(name), permission)} {}

    [[nodiscard]] const tools::ToolDefinition& definition() const override {
        return m_definition;
    }

    tools::ToolResult execute(const tools::ValidatedCall& call,
                              const std::atomic<bool>&) override {
        return tools::ToolResult::success(call.toolName(), {{"ran", "true"}});
    }

private:
    tools::ToolDefinition m_definition;

    static tools::ToolDefinition makeDefinition(std::string name,
                                                tools::PermissionLevel permission) {
        tools::ToolDefinition definition;
        definition.name = std::move(name);
        definition.description = "fixture";
        definition.permission = permission;

        tools::ArgumentSpec target;
        target.name = "target";
        target.type = tools::ArgumentType::Enumeration;
        target.required = false;
        target.allowedValues = {"alpha", "beta"};
        definition.arguments.push_back(std::move(target));

        tools::ArgumentSpec level;
        level.name = "level";
        level.type = tools::ArgumentType::Integer;
        level.required = false;
        level.minimum = 0;
        level.maximum = 10;
        definition.arguments.push_back(std::move(level));

        return definition;
    }
};

} // namespace

class TestAgentPlanner : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // --- accepted plans ----------------------------------------------------
    void acceptsASingleStepPlan();
    void acceptsAMultiStepPlanInOrder();
    void acceptsAPlanWithoutTheTypeField();
    void readsPermissionsFromTheRegistryNotThePlan();
    void findsAPlanInSurroundingProse();
    void tellsAPlanApartFromASingleCall();

    // --- malformed envelopes ----------------------------------------------
    void rejectsMalformedJson();
    void rejectsANonObject();
    void rejectsAWrongTypeField();
    void rejectsMissingSteps();
    void rejectsStepsOfTheWrongShape();
    void rejectsAnEmptyPlan();
    void rejectsAnOversizedPayload();

    // --- the attacks -------------------------------------------------------
    void rejectsExtraTopLevelFields_data();
    void rejectsExtraTopLevelFields();
    void rejectsForgedFieldsInsideAStep_data();
    void rejectsForgedFieldsInsideAStep();
    void rejectsAnUnknownToolAnywhereInThePlan();
    void rejectsAnExecutablePathAsAnArgument();
    void rejectsShellPayloadsInEveryPosition_data();
    void rejectsShellPayloadsInEveryPosition();
    void oneBadStepRejectsTheWholePlan();
    void rejectsMoreStepsThanTheLimit();
    void theStepLimitCannotBeWidened();
    void rejectsADeniedTool();

    // --- populating a task -------------------------------------------------
    void populatesATaskInOrder();
    void refusesToPopulateATaskThatHasStarted();

private:
    tools::ToolRegistry m_registry;
    std::unique_ptr<tools::ToolValidator> m_validator;
    std::unique_ptr<Planner> m_planner;

    void expectRejection(const std::string& json, PlanError expected,
                         const char* what) const;
};

void TestAgentPlanner::initTestCase() {
    QVERIFY(m_registry.add(std::make_unique<FixtureTool>(
        "read_thing", tools::PermissionLevel::ReadOnly)));
    QVERIFY(m_registry.add(std::make_unique<FixtureTool>(
        "confirm_thing", tools::PermissionLevel::ConfirmRequired)));
    QVERIFY(m_registry.add(std::make_unique<FixtureTool>(
        "denied_thing", tools::PermissionLevel::Denied)));

    m_validator = std::make_unique<tools::ToolValidator>(m_registry);
    m_planner = std::make_unique<Planner>(m_registry, *m_validator);
}

void TestAgentPlanner::expectRejection(const std::string& json, PlanError expected,
                                       const char* what) const {
    const auto result = m_planner->parse(json);
    QVERIFY2(!result.has_value(),
             qPrintable(QStringLiteral("accepted a plan it must refuse: %1")
                            .arg(QString::fromUtf8(what))));
    QCOMPARE(QString::fromUtf8(planErrorName(result.error().code).data()),
             QString::fromUtf8(planErrorName(expected).data()));
}

// ---------------------------------------------------------------------------
// Accepted plans
// ---------------------------------------------------------------------------

void TestAgentPlanner::acceptsASingleStepPlan() {
    const auto plan = m_planner->parse(
        R"({"type":"plan","steps":[{"tool":"read_thing","arguments":{}}]})");

    QVERIFY(plan.has_value());
    QCOMPARE(plan->size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(plan->steps[0].call.toolName()),
             QStringLiteral("read_thing"));
    QCOMPARE(plan->steps[0].permission, tools::PermissionLevel::ReadOnly);
    QVERIFY(!plan->needsConfirmation());
}

void TestAgentPlanner::acceptsAMultiStepPlanInOrder() {
    const auto plan = m_planner->parse(R"({
        "type": "plan",
        "steps": [
            {"tool": "read_thing", "arguments": {"target": "alpha"}},
            {"tool": "read_thing", "arguments": {"level": 5}},
            {"tool": "confirm_thing", "arguments": {"target": "beta"}}
        ]
    })");

    QVERIFY(plan.has_value());
    QCOMPARE(plan->size(), std::size_t{3});

    QCOMPARE(QString::fromStdString(plan->steps[0].call.enumerationArgument("target")),
             QStringLiteral("alpha"));
    QCOMPARE(plan->steps[1].call.integerArgument("level", -1), std::int64_t{5});
    QCOMPARE(QString::fromStdString(plan->steps[2].call.toolName()),
             QStringLiteral("confirm_thing"));

    // A plan containing a confirm-required step says so, so the agent can warn
    // before starting rather than surprising the user in the middle.
    QVERIFY(plan->needsConfirmation());
}

void TestAgentPlanner::acceptsAPlanWithoutTheTypeField() {
    // "type" is a courtesy, not a requirement. Refusing a plan for the absence
    // of a constant string would fail on a technicality.
    const auto plan =
        m_planner->parse(R"({"steps":[{"tool":"read_thing","arguments":{}}]})");
    QVERIFY(plan.has_value());
    QCOMPARE(plan->size(), std::size_t{1});
}

void TestAgentPlanner::readsPermissionsFromTheRegistryNotThePlan() {
    // The plan has no field that could carry a permission. Even if it had, the
    // level attached to each step comes from the tool's own definition.
    const auto plan = m_planner->parse(
        R"({"steps":[{"tool":"confirm_thing","arguments":{}}]})");

    QVERIFY(plan.has_value());
    QCOMPARE(plan->steps[0].permission, tools::PermissionLevel::ConfirmRequired);
    QCOMPARE(m_registry.lookup("confirm_thing")->definition().permission,
             tools::PermissionLevel::ConfirmRequired);
}

void TestAgentPlanner::findsAPlanInSurroundingProse() {
    const std::string output =
        "I will do this in two steps.\n```json\n"
        R"({"type":"plan","steps":[{"tool":"read_thing","arguments":{}}]})"
        "\n```\nStarting now.";

    const std::string json = Planner::extractPlanJson(output);
    QVERIFY(!json.empty());
    QVERIFY(m_planner->parse(json).has_value());
}

void TestAgentPlanner::tellsAPlanApartFromASingleCall() {
    // Lets the agent take the single-step path without a planner round trip -
    // which is worth a whole generation, several seconds on this machine.
    QVERIFY(Planner::looksLikePlan(
        R"({"type":"plan","steps":[{"tool":"read_thing"}]})"));
    QVERIFY(!Planner::looksLikePlan(R"({"tool":"read_thing","arguments":{}})"));
    QVERIFY(!Planner::looksLikePlan("Your machine has 32 GB of memory."));
}

// ---------------------------------------------------------------------------
// Malformed envelopes
// ---------------------------------------------------------------------------

void TestAgentPlanner::rejectsMalformedJson() {
    expectRejection(R"({"steps":[)", PlanError::MalformedJson, "truncated");
    expectRejection("not json", PlanError::MalformedJson, "prose");
    expectRejection("", PlanError::MalformedJson, "empty");
}

void TestAgentPlanner::rejectsANonObject() {
    expectRejection(R"([{"tool":"read_thing"}])", PlanError::NotAnObject, "array");
}

void TestAgentPlanner::rejectsAWrongTypeField() {
    expectRejection(R"({"type":"execute","steps":[{"tool":"read_thing"}]})",
                    PlanError::WrongType, "type is not \"plan\"");
    expectRejection(R"({"type":42,"steps":[{"tool":"read_thing"}]})",
                    PlanError::WrongType, "type is a number");
}

void TestAgentPlanner::rejectsMissingSteps() {
    expectRejection(R"({"type":"plan"})", PlanError::MissingSteps, "no steps key");
}

void TestAgentPlanner::rejectsStepsOfTheWrongShape() {
    expectRejection(R"({"steps":"read_thing"})", PlanError::WrongType,
                    "steps is a string");
    expectRejection(R"({"steps":{"tool":"read_thing"}})", PlanError::WrongType,
                    "steps is an object");
    expectRejection(R"({"steps":["read_thing"]})", PlanError::WrongType,
                    "a step is a string");
    expectRejection(R"({"steps":[42]})", PlanError::WrongType, "a step is a number");
}

void TestAgentPlanner::rejectsAnEmptyPlan() {
    // An empty plan would put the agent into Running with nothing to do.
    expectRejection(R"({"type":"plan","steps":[]})", PlanError::EmptyPlan, "no steps");
}

void TestAgentPlanner::rejectsAnOversizedPayload() {
    std::string huge = R"({"type":"plan","steps":[{"tool":"read_thing","arguments":{"target":")";
    huge.append(Planner::kMaxPlanLength, 'a');
    huge += R"("}}]})";

    expectRejection(huge, PlanError::TooLarge, "oversized plan");
}

// ---------------------------------------------------------------------------
// The attacks
// ---------------------------------------------------------------------------

void TestAgentPlanner::rejectsExtraTopLevelFields_data() {
    QTest::addColumn<QString>("field");

    // None of these words appears in the planner. They are refused because only
    // "type" and "steps" exist, so the next invented field needs no new code.
    for (const char* field : {"confirmed", "permission", "approved", "admin",
                              "skip_confirmation", "user_approved", "elevated",
                              "task_id", "step_id", "grant", "token", "policy"}) {
        QTest::newRow(field) << QString::fromUtf8(field);
    }
}

void TestAgentPlanner::rejectsExtraTopLevelFields() {
    QFETCH(QString, field);

    const QString json =
        QStringLiteral(R"({"steps":[{"tool":"read_thing"}],"%1":true})").arg(field);
    expectRejection(json.toStdString(), PlanError::UnknownField, "extra plan field");
}

void TestAgentPlanner::rejectsForgedFieldsInsideAStep_data() {
    QTest::addColumn<QString>("field");

    for (const char* field : {"confirmed", "permission", "approved", "admin",
                              "skip_confirmation", "task_id", "step_id", "grant"}) {
        QTest::newRow(field) << QString::fromUtf8(field);
    }
}

void TestAgentPlanner::rejectsForgedFieldsInsideAStep() {
    QFETCH(QString, field);

    // A step is validated by ToolValidator, which accepts only "tool" and
    // "arguments". The plan path gets no gentler treatment than a single call.
    const QString json =
        QStringLiteral(R"({"steps":[{"tool":"confirm_thing","%1":true}]})").arg(field);
    expectRejection(json.toStdString(), PlanError::InvalidStep, "forged step field");
}

void TestAgentPlanner::rejectsAnUnknownToolAnywhereInThePlan() {
    // First, middle and last. A good step must not be able to carry a bad one.
    expectRejection(R"({"steps":[{"tool":"run_shell"},{"tool":"read_thing"}]})",
                    PlanError::InvalidStep, "unknown tool first");
    expectRejection(
        R"({"steps":[{"tool":"read_thing"},{"tool":"powershell"},{"tool":"read_thing"}]})",
        PlanError::InvalidStep, "unknown tool in the middle");
    expectRejection(R"({"steps":[{"tool":"read_thing"},{"tool":"delete_file"}]})",
                    PlanError::InvalidStep, "unknown tool last");
}

void TestAgentPlanner::rejectsAnExecutablePathAsAnArgument() {
    expectRejection(
        R"({"steps":[{"tool":"confirm_thing","arguments":{"target":"C:\\Windows\\System32\\cmd.exe"}}]})",
        PlanError::InvalidStep, "path as an enumeration value");
    expectRejection(
        R"({"steps":[{"tool":"confirm_thing","arguments":{"path":"C:\\Windows"}}]})",
        PlanError::InvalidStep, "path as an argument name");
}

void TestAgentPlanner::rejectsShellPayloadsInEveryPosition_data() {
    QTest::addColumn<QString>("json");

    QTest::newRow("as a tool name")
        << R"({"steps":[{"tool":"cmd.exe /c del C:\\Windows"}]})";
    QTest::newRow("as an enumeration value")
        << R"({"steps":[{"tool":"read_thing","arguments":{"target":"alpha; rm -rf /"}}]})";
    QTest::newRow("as an unknown argument")
        << R"({"steps":[{"tool":"read_thing","arguments":{"command":"powershell -enc AAAA"}}]})";
    QTest::newRow("as an extra plan field")
        << R"({"steps":[{"tool":"read_thing"}],"exec":"cmd.exe"})";
    QTest::newRow("as a traversal tool name")
        << R"({"steps":[{"tool":"../../../Windows/System32/cmd.exe"}]})";
    QTest::newRow("as a nested object")
        << R"({"steps":[{"tool":"read_thing","arguments":{"level":{"$exec":"cmd"}}}]})";
}

void TestAgentPlanner::rejectsShellPayloadsInEveryPosition() {
    QFETCH(QString, json);

    const auto result = m_planner->parse(json.toStdString());
    QVERIFY2(!result.has_value(), "a shell payload survived planning");
}

void TestAgentPlanner::oneBadStepRejectsTheWholePlan() {
    // Four good steps and one bad one, fourth of five - the shape a model would
    // produce if it were trying to hide something in the middle of useful work.
    const auto result = m_planner->parse(R"({
        "type": "plan",
        "steps": [
            {"tool": "read_thing", "arguments": {"target": "alpha"}},
            {"tool": "read_thing", "arguments": {"target": "beta"}},
            {"tool": "read_thing", "arguments": {"level": 1}},
            {"tool": "read_thing", "arguments": {"level": 99}},
            {"tool": "read_thing", "arguments": {}}
        ]
    })");

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, PlanError::InvalidStep);

    // The index is reported so the audit trail and the user can be told which
    // step was at fault, not merely that something was.
    QCOMPARE(result.error().stepIndex, std::size_t{3});

    // And nothing partial escaped: parse() returns a plan or nothing at all.
    QVERIFY(!result.has_value());
}

void TestAgentPlanner::rejectsMoreStepsThanTheLimit() {
    std::string json = R"({"type":"plan","steps":[)";
    for (std::size_t i = 0; i <= Planner::kMaxPlanSteps; ++i) {
        if (i > 0) {
            json += ',';
        }
        json += R"({"tool":"read_thing","arguments":{}})";
    }
    json += "]}";

    expectRejection(json, PlanError::TooManySteps, "one step over the limit");
}

void TestAgentPlanner::theStepLimitCannotBeWidened() {
    Planner planner{m_registry, *m_validator};

    // Configuration may narrow the limit.
    planner.setMaxSteps(2);
    QCOMPARE(planner.maxSteps(), std::size_t{2});

    // It can never widen it past the structural ceiling. A config file asking
    // for a thousand steps gets the ceiling, not a thousand.
    planner.setMaxSteps(1000);
    QCOMPARE(planner.maxSteps(), Planner::kMaxPlanSteps);
    QCOMPARE(Planner::kMaxPlanSteps, Task::kMaxSteps);

    // And zero is not a way to disable the check into something meaningless.
    planner.setMaxSteps(0);
    QCOMPARE(planner.maxSteps(), std::size_t{1});
}

void TestAgentPlanner::rejectsADeniedTool() {
    // The tool exists, so the call validates - and is then refused for its
    // level. Letting it into a plan would show the user a step that can never
    // run, and would give a model a way to occupy the plan with dead weight.
    expectRejection(R"({"steps":[{"tool":"denied_thing","arguments":{}}]})",
                    PlanError::DeniedTool, "denied tool in a plan");
    expectRejection(
        R"({"steps":[{"tool":"read_thing"},{"tool":"denied_thing"}]})",
        PlanError::DeniedTool, "denied tool after a good step");
}

// ---------------------------------------------------------------------------
// Populating a task
// ---------------------------------------------------------------------------

void TestAgentPlanner::populatesATaskInOrder() {
    const auto plan = m_planner->parse(R"({
        "steps": [
            {"tool": "read_thing", "arguments": {"target": "alpha"}},
            {"tool": "confirm_thing", "arguments": {"target": "beta"}}
        ]
    })");
    QVERIFY(plan.has_value());

    Task task{"check and open", TaskOrigin::Text};
    const auto populated = m_planner->populate(task, *plan);
    QVERIFY(populated.has_value());

    QCOMPARE(task.totalSteps(), std::size_t{2});
    QCOMPARE(QString::fromStdString(task.steps()[0].toolName()),
             QStringLiteral("read_thing"));
    QCOMPARE(QString::fromStdString(task.steps()[1].toolName()),
             QStringLiteral("confirm_thing"));

    // The confirmation requirement travels with the step, taken from the
    // registry rather than from anything the model wrote.
    QVERIFY(!task.steps()[0].requiresConfirmation());
    QVERIFY(task.steps()[1].requiresConfirmation());
}

void TestAgentPlanner::refusesToPopulateATaskThatHasStarted() {
    const auto plan =
        m_planner->parse(R"({"steps":[{"tool":"read_thing","arguments":{}}]})");
    QVERIFY(plan.has_value());

    Task task{"already going", TaskOrigin::Text};
    QVERIFY(m_planner->populate(task, *plan).has_value());
    QVERIFY(task.start());

    // A plan cannot be grafted onto a running task: the plan the user was shown
    // must be the plan that runs.
    const auto second = m_planner->populate(task, *plan);
    QVERIFY(!second.has_value());
    QCOMPARE(task.totalSteps(), std::size_t{1});
}

QTEST_MAIN(TestAgentPlanner)
#include "tst_agent_planner.moc"
