// The safety gates, extended for tasks.
//
// Phase 5 bound an approval to a tool and its arguments. That was enough when
// there was one call at a time. An agent runs several steps, sometimes the same
// tool twice, so the binding has to reach further: an approval names a place in
// a task, and nowhere else.
//
// Also here: the Destructive permission level, which always asks and which no
// combination of settings can turn into an automatic action.

#include <QtTest/QtTest>

#include <atomic>
#include <memory>
#include <string>

#include "jarvis/agent/Planner.h"
#include "jarvis/agent/Task.h"
#include "jarvis/tools/AuditLog.h"
#include "jarvis/tools/Confirmation.h"
#include "jarvis/tools/PermissionManager.h"
#include "jarvis/tools/ToolExecutor.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

using namespace jarvis;
using namespace jarvis::agent;
using namespace std::chrono_literals;

namespace {

class SpyTool final : public tools::ITool {
public:
    SpyTool(std::string name, tools::PermissionLevel permission)
        : m_definition{makeDefinition(std::move(name), permission)} {}

    [[nodiscard]] const tools::ToolDefinition& definition() const override {
        return m_definition;
    }

    tools::ToolResult execute(const tools::ValidatedCall& call,
                              const std::atomic<bool>&) override {
        ++executions;
        return tools::ToolResult::success(call.toolName(), {{"ran", "true"}});
    }

    std::atomic<int> executions{0};

private:
    tools::ToolDefinition m_definition;

    static tools::ToolDefinition makeDefinition(std::string name,
                                                tools::PermissionLevel permission) {
        tools::ToolDefinition definition;
        definition.name = std::move(name);
        definition.description = "spy";
        definition.permission = permission;

        tools::ArgumentSpec target;
        target.name = "target";
        target.type = tools::ArgumentType::Enumeration;
        target.required = false;
        target.allowedValues = {"alpha", "beta"};
        definition.arguments.push_back(std::move(target));

        return definition;
    }
};

} // namespace

class TestAgentGates : public QObject {
    Q_OBJECT

private slots:
    void init();

    // --- the Destructive level ---------------------------------------------
    void destructiveHasItsOwnName();
    void destructiveIsNeverAllowedByAnyPolicy();
    void destructiveIsOffByDefaultAndAsksWhenOn();
    void destructiveAlwaysCountsAsNeedingConfirmation();
    void existingLevelsAreUnchanged();

    // --- scoped fingerprints -----------------------------------------------
    void scopeChangesTheFingerprint();
    void anEmptyScopeIsStillDistinctFromAScopedOne();
    void approvalCannotBeReplayedInAnotherTask();
    void approvalCannotBeReplayedAtAnotherStep();
    void approvalCannotBeReplayedWithOtherArguments();
    void approvalCannotBeReplayedForAnotherTool();
    void approvalWorksAtTheStepItWasGivenFor();
    void anUnscopedApprovalDoesNotUnlockATaskStep();

private:
    std::unique_ptr<tools::ToolRegistry> m_registry;
    tools::PermissionManager m_permissions;
    tools::AuditLog m_audit;
    std::unique_ptr<tools::ToolValidator> m_validator;
    std::unique_ptr<tools::ToolExecutor> m_executor;

    SpyTool* m_confirmTool{nullptr};
    SpyTool* m_destructiveTool{nullptr};

    [[nodiscard]] tools::ValidatedCall parse(const std::string& json) const;
    [[nodiscard]] tools::ValidatedCall confirmCall(const char* target = "alpha") const;
};

tools::ValidatedCall TestAgentGates::parse(const std::string& json) const {
    auto result = m_validator->validate(json);
    if (!result.has_value()) {
        qFatal("fixture produced an invalid call: %s", json.c_str());
    }
    return std::move(*result);
}

tools::ValidatedCall TestAgentGates::confirmCall(const char* target) const {
    return parse(std::string{R"({"tool":"confirm_spy","arguments":{"target":")"} + target
                 + R"("}})");
}

void TestAgentGates::init() {
    m_registry = std::make_unique<tools::ToolRegistry>();
    m_audit.clear();

    auto confirmTool =
        std::make_unique<SpyTool>("confirm_spy", tools::PermissionLevel::ConfirmRequired);
    auto destructiveTool =
        std::make_unique<SpyTool>("destroy_spy", tools::PermissionLevel::Destructive);

    m_confirmTool = confirmTool.get();
    m_destructiveTool = destructiveTool.get();

    QVERIFY(m_registry->add(std::move(confirmTool)));
    QVERIFY(m_registry->add(std::move(destructiveTool)));
    QVERIFY(m_registry->add(std::make_unique<SpyTool>(
        "read_spy", tools::PermissionLevel::ReadOnly)));

    m_permissions.setPolicy({});
    m_validator = std::make_unique<tools::ToolValidator>(*m_registry);
    m_executor =
        std::make_unique<tools::ToolExecutor>(*m_registry, m_permissions, m_audit);
}

// ---------------------------------------------------------------------------
// The Destructive level
// ---------------------------------------------------------------------------

void TestAgentGates::destructiveHasItsOwnName() {
    QCOMPARE(QString::fromUtf8(
                 tools::permissionName(tools::PermissionLevel::Destructive).data()),
             QStringLiteral("DESTRUCTIVE"));

    // The existing names are untouched: they appear in the audit log, in the
    // interface and in every Phase 5 test.
    QCOMPARE(QString::fromUtf8(
                 tools::permissionName(tools::PermissionLevel::ConfirmRequired).data()),
             QStringLiteral("CONFIRM_REQUIRED"));
}

void TestAgentGates::destructiveIsNeverAllowedByAnyPolicy() {
    // All thirty-two combinations of the five flags. Not one of them yields
    // Allow: an irreversible action is either refused or offered for approval,
    // and there is no third answer.
    for (int mask = 0; mask < 32; ++mask) {
        tools::PermissionManager::Policy policy;
        policy.toolsEnabled = (mask & 1) != 0;
        policy.allowReadOnly = (mask & 2) != 0;
        policy.allowSafeActions = (mask & 4) != 0;
        policy.allowConfirmedActions = (mask & 8) != 0;
        policy.allowDestructiveActions = (mask & 16) != 0;

        const tools::PermissionManager manager{policy};
        const auto verdict = manager.evaluate(tools::PermissionLevel::Destructive);

        QVERIFY2(verdict.decision != tools::PermissionManager::Decision::Allow,
                 qPrintable(QStringLiteral("policy mask %1 allowed a destructive "
                                           "action automatically")
                                .arg(mask)));
    }
}

void TestAgentGates::destructiveIsOffByDefaultAndAsksWhenOn() {
    const tools::PermissionManager defaults{};
    QCOMPARE(defaults.policy().allowDestructiveActions, false);
    QCOMPARE(defaults.evaluate(tools::PermissionLevel::Destructive).decision,
             tools::PermissionManager::Decision::Deny);

    tools::PermissionManager::Policy enabled;
    enabled.allowDestructiveActions = true;
    const tools::PermissionManager manager{enabled};
    QCOMPARE(manager.evaluate(tools::PermissionLevel::Destructive).decision,
             tools::PermissionManager::Decision::RequireConfirmation);

    // Switching confirmations off refuses destructive actions outright rather
    // than promoting them to silent ones.
    enabled.allowConfirmedActions = false;
    const tools::PermissionManager restricted{enabled};
    QCOMPARE(restricted.evaluate(tools::PermissionLevel::Destructive).decision,
             tools::PermissionManager::Decision::Deny);
}

void TestAgentGates::destructiveAlwaysCountsAsNeedingConfirmation() {
    Task task{"destroy something", TaskOrigin::Text};
    const StepId id = task.addStep(parse(R"({"tool":"destroy_spy","arguments":{}})"),
                                   tools::PermissionLevel::Destructive);

    QVERIFY(id.valid());
    QVERIFY(task.findStep(id)->requiresConfirmation());
}

void TestAgentGates::existingLevelsAreUnchanged() {
    // Adding a level must not have shifted the meaning of the others. This is
    // the compatibility check the whole Phase 5 suite depends on.
    tools::PermissionManager::Policy policy;
    const tools::PermissionManager manager{policy};

    QCOMPARE(manager.evaluate(tools::PermissionLevel::ReadOnly).decision,
             tools::PermissionManager::Decision::Allow);
    QCOMPARE(manager.evaluate(tools::PermissionLevel::SafeAction).decision,
             tools::PermissionManager::Decision::Allow);
    QCOMPARE(manager.evaluate(tools::PermissionLevel::ConfirmRequired).decision,
             tools::PermissionManager::Decision::RequireConfirmation);
    QCOMPARE(manager.evaluate(tools::PermissionLevel::Denied).decision,
             tools::PermissionManager::Decision::Deny);
}

// ---------------------------------------------------------------------------
// Scoped fingerprints
// ---------------------------------------------------------------------------

void TestAgentGates::scopeChangesTheFingerprint() {
    const tools::ValidatedCall call = confirmCall();

    const std::string atStepOne = stepIdentity(TaskId{1}, StepId{1}, call);
    const std::string atStepTwo = stepIdentity(TaskId{1}, StepId{2}, call);
    const std::string inTaskTwo = stepIdentity(TaskId{2}, StepId{1}, call);

    QVERIFY(tools::scopedFingerprint(atStepOne, call)
            != tools::scopedFingerprint(atStepTwo, call));
    QVERIFY(tools::scopedFingerprint(atStepOne, call)
            != tools::scopedFingerprint(inTaskTwo, call));

    // The same place with the same call is the same thing to approve.
    QCOMPARE(tools::scopedFingerprint(atStepOne, call),
             tools::scopedFingerprint(stepIdentity(TaskId{1}, StepId{1}, call), call));
}

void TestAgentGates::anEmptyScopeIsStillDistinctFromAScopedOne() {
    const tools::ValidatedCall call = confirmCall();

    // A call made outside any task carries an empty scope. It must not be
    // interchangeable with the same call inside one.
    QVERIFY(tools::scopedFingerprint({}, call)
            != tools::scopedFingerprint(stepIdentity(TaskId{1}, StepId{1}, call), call));

    // And the unscoped form still distinguishes arguments, as it did before.
    QVERIFY(tools::scopedFingerprint({}, confirmCall("alpha"))
            != tools::scopedFingerprint({}, confirmCall("beta")));
}

void TestAgentGates::approvalCannotBeReplayedInAnotherTask() {
    tools::ConfirmationStore store;
    const tools::ValidatedCall call = confirmCall();

    const std::string approvedScope = stepIdentity(TaskId{1}, StepId{1}, call);
    const std::uint64_t id = store.createRequest(call, 30s, approvedScope);
    auto grant = store.approve(id);
    QVERIFY(grant.has_value());

    // The user approved this call at step 1 of task 1. Presenting it for the
    // identical call at step 1 of task 2 must fail.
    const std::string otherScope = stepIdentity(TaskId{2}, StepId{1}, call);
    const std::atomic<bool> notCancelled{false};
    const tools::ToolResult result =
        m_executor->execute(call, notCancelled, id, std::move(grant), otherScope);

    QVERIFY(!result.ok());
    QCOMPARE(result.errorCode(), tools::ToolErrorCode::ConfirmationInvalid);
    QCOMPARE(m_confirmTool->executions.load(), 0);
}

void TestAgentGates::approvalCannotBeReplayedAtAnotherStep() {
    tools::ConfirmationStore store;
    const tools::ValidatedCall call = confirmCall();

    // The hardest case: the same task, the same tool, the same arguments -
    // only the step differs. Without the step in the fingerprint this would
    // succeed, and an approval for "open the calculator now" would silently
    // cover "open it again later in the same task".
    const std::uint64_t id =
        store.createRequest(call, 30s, stepIdentity(TaskId{9}, StepId{3}, call));
    auto grant = store.approve(id);
    QVERIFY(grant.has_value());

    const std::atomic<bool> notCancelled{false};
    const tools::ToolResult result =
        m_executor->execute(call, notCancelled, id, std::move(grant),
                            stepIdentity(TaskId{9}, StepId{4}, call));

    QVERIFY(!result.ok());
    QCOMPARE(result.errorCode(), tools::ToolErrorCode::ConfirmationInvalid);
    QCOMPARE(m_confirmTool->executions.load(), 0);
}

void TestAgentGates::approvalCannotBeReplayedWithOtherArguments() {
    tools::ConfirmationStore store;
    const tools::ValidatedCall approved = confirmCall("alpha");
    const tools::ValidatedCall swapped = confirmCall("beta");

    const std::string scope = stepIdentity(TaskId{1}, StepId{1}, approved);
    const std::uint64_t id = store.createRequest(approved, 30s, scope);
    auto grant = store.approve(id);
    QVERIFY(grant.has_value());

    const std::atomic<bool> notCancelled{false};
    const tools::ToolResult result =
        m_executor->execute(swapped, notCancelled, id, std::move(grant), scope);

    QVERIFY(!result.ok());
    QCOMPARE(result.errorCode(), tools::ToolErrorCode::ConfirmationInvalid);
    QCOMPARE(m_confirmTool->executions.load(), 0);
}

void TestAgentGates::approvalCannotBeReplayedForAnotherTool() {
    tools::ConfirmationStore store;
    const tools::ValidatedCall approved = confirmCall();
    const tools::ValidatedCall other = parse(R"({"tool":"destroy_spy","arguments":{}})");

    const std::string scope = stepIdentity(TaskId{1}, StepId{1}, approved);
    const std::uint64_t id = store.createRequest(approved, 30s, scope);
    auto grant = store.approve(id);
    QVERIFY(grant.has_value());

    // Destructive is off by default, so this is refused for its permission
    // before the fingerprint is even consulted - which is the right order.
    const std::atomic<bool> notCancelled{false};
    const tools::ToolResult result =
        m_executor->execute(other, notCancelled, id, std::move(grant), scope);

    QVERIFY(!result.ok());
    QCOMPARE(m_destructiveTool->executions.load(), 0);
}

void TestAgentGates::approvalWorksAtTheStepItWasGivenFor() {
    // The positive case. Without it the tests above would pass on a system that
    // simply refuses everything.
    tools::ConfirmationStore store;
    const tools::ValidatedCall call = confirmCall();

    const std::string scope = stepIdentity(TaskId{4}, StepId{2}, call);
    const std::uint64_t id = store.createRequest(call, 30s, scope);
    auto grant = store.approve(id);
    QVERIFY(grant.has_value());

    const std::atomic<bool> notCancelled{false};
    const tools::ToolResult result =
        m_executor->execute(call, notCancelled, id, std::move(grant), scope);

    QVERIFY(result.ok());
    QCOMPARE(m_confirmTool->executions.load(), 1);
}

void TestAgentGates::anUnscopedApprovalDoesNotUnlockATaskStep() {
    tools::ConfirmationStore store;
    const tools::ValidatedCall call = confirmCall();

    // Approved as a bare conversational call, then presented as a task step.
    const std::uint64_t id = store.createRequest(call, 30s);
    auto grant = store.approve(id);
    QVERIFY(grant.has_value());

    const std::atomic<bool> notCancelled{false};
    const tools::ToolResult result =
        m_executor->execute(call, notCancelled, id, std::move(grant),
                            stepIdentity(TaskId{1}, StepId{1}, call));

    QVERIFY(!result.ok());
    QCOMPARE(result.errorCode(), tools::ToolErrorCode::ConfirmationInvalid);
    QCOMPARE(m_confirmTool->executions.load(), 0);
}

QTEST_MAIN(TestAgentGates)
#include "tst_agent_gates.moc"
