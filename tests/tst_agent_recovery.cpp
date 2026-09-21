// The recovery policy.
//
// Most of these tests are about what recovery refuses to do. Retrying a reading
// that was momentarily unavailable is convenience; refusing to repeat an
// irreversible action whose outcome is unknown is not. The two live in the same
// class because the decision is the same shape - and putting the safety rules
// anywhere other than beside the convenience rule is how one of them gets
// forgotten.

#include <QtTest/QtTest>

#include <memory>
#include <string>

#include "jarvis/agent/Recovery.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

using namespace jarvis;
using namespace jarvis::agent;
using Action = RecoveryPolicy::Action;

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
        return definition;
    }
};

} // namespace

class TestAgentRecovery : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // --- classification ----------------------------------------------------
    void everyToolErrorIsClassified();
    void transientErrorsAreTheOnlyRetryableOnes();

    // --- the ordinary case -------------------------------------------------
    void retryableErrorRetries();
    void attemptsAreMonotonic();
    void retryLimitIsEnforced();
    void nonRetryableErrorDoesNotRetry_data();
    void nonRetryableErrorDoesNotRetry();

    // --- the safety rules --------------------------------------------------
    void destructiveStepCannotAutomaticallyRetry();
    void confirmRequiredStepCannotAutomaticallyRetry();
    void confirmationRefusalIsFinal();
    void deniedActionIsNeverRetried();
    void cancelledTaskCannotRetry();
    void aFinishedTaskCannotGainWork();
    void successfulStepCannotRetry();
    void contextOverflowIsNotRetried();

    // --- bounds ------------------------------------------------------------
    void backoffIsBoundedAndGrows();
    void attemptCeilingCannotBeRaised();
    void disabledRecoverySkipsRatherThanRetries();

private:
    tools::ToolRegistry m_registry;
    std::unique_ptr<tools::ToolValidator> m_validator;

    [[nodiscard]] Task taskWith(tools::PermissionLevel permission,
                                const char* tool = "read_thing");
    [[nodiscard]] tools::ValidatedCall call(const char* tool) const;
};

tools::ValidatedCall TestAgentRecovery::call(const char* tool) const {
    auto result = m_validator->validate(std::string{R"({"tool":")"} + tool + R"("})");
    if (!result.has_value()) {
        qFatal("fixture produced an invalid call for %s", tool);
    }
    return std::move(*result);
}

Task TestAgentRecovery::taskWith(tools::PermissionLevel permission, const char* tool) {
    Task task{"do the thing", TaskOrigin::Text};
    task.addStep(call(tool), permission);
    task.start();
    return task;
}

void TestAgentRecovery::initTestCase() {
    QVERIFY(m_registry.add(std::make_unique<FixtureTool>(
        "read_thing", tools::PermissionLevel::ReadOnly)));
    QVERIFY(m_registry.add(std::make_unique<FixtureTool>(
        "confirm_thing", tools::PermissionLevel::ConfirmRequired)));
    QVERIFY(m_registry.add(std::make_unique<FixtureTool>(
        "destroy_thing", tools::PermissionLevel::Destructive)));

    m_validator = std::make_unique<tools::ToolValidator>(m_registry);
}

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

void TestAgentRecovery::everyToolErrorIsClassified() {
    // A code that fell through to a default would be silently treated as
    // whatever that default is. Checking every one means adding a code forces a
    // decision about what it means for recovery.
    const std::vector<tools::ToolErrorCode> all{
        tools::ToolErrorCode::None,
        tools::ToolErrorCode::MalformedJson,
        tools::ToolErrorCode::NotAnObject,
        tools::ToolErrorCode::MissingToolField,
        tools::ToolErrorCode::UnknownTool,
        tools::ToolErrorCode::MissingArguments,
        tools::ToolErrorCode::ArgumentsNotAnObject,
        tools::ToolErrorCode::UnknownArgument,
        tools::ToolErrorCode::MissingRequiredArgument,
        tools::ToolErrorCode::WrongArgumentType,
        tools::ToolErrorCode::ValueOutOfRange,
        tools::ToolErrorCode::ValueNotAllowed,
        tools::ToolErrorCode::PermissionDenied,
        tools::ToolErrorCode::ConfirmationRequired,
        tools::ToolErrorCode::ConfirmationInvalid,
        tools::ToolErrorCode::Timeout,
        tools::ToolErrorCode::Cancelled,
        tools::ToolErrorCode::Unavailable,
        tools::ToolErrorCode::ExecutionFailed,
    };

    for (const tools::ToolErrorCode code : all) {
        const FailureKind kind = classifyFailure(code);
        if (code == tools::ToolErrorCode::None) {
            QCOMPARE(kind, FailureKind::None);
        } else {
            QVERIFY2(kind != FailureKind::None,
                     qPrintable(QStringLiteral("%1 was not classified")
                                    .arg(QString::fromUtf8(
                                        tools::toolErrorName(code).data()))));
        }
    }
}

void TestAgentRecovery::transientErrorsAreTheOnlyRetryableOnes() {
    QCOMPARE(classifyFailure(tools::ToolErrorCode::Unavailable), FailureKind::Transient);
    QCOMPARE(classifyFailure(tools::ToolErrorCode::Timeout), FailureKind::Transient);

    // A wrong argument is wrong however many times it is sent.
    QCOMPARE(classifyFailure(tools::ToolErrorCode::WrongArgumentType),
             FailureKind::Permanent);
    QCOMPARE(classifyFailure(tools::ToolErrorCode::UnknownTool), FailureKind::Permanent);
    QCOMPARE(classifyFailure(tools::ToolErrorCode::PermissionDenied),
             FailureKind::Denied);
    QCOMPARE(classifyFailure(tools::ToolErrorCode::Cancelled), FailureKind::Cancelled);
}

// ---------------------------------------------------------------------------
// The ordinary case
// ---------------------------------------------------------------------------

void TestAgentRecovery::retryableErrorRetries() {
    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "the reading was unavailable"));

    const auto decision = policy.evaluate(FailureKind::Transient, *task.findStep(id),
                                          task.status());
    QCOMPARE(decision.action, Action::Retry);
    QVERIFY(decision.delay.count() > 0);
    QVERIFY(!decision.reason.empty());
}

void TestAgentRecovery::attemptsAreMonotonic() {
    RecoveryPolicy policy{{.enabled = true, .maxAttempts = 4, .baseBackoff = {}}};
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    int previous = 0;
    for (int round = 0; round < 3; ++round) {
        QVERIFY(task.beginStep(id));
        const int attempts = task.findStep(id)->attempts();

        // Never reset. Resetting would make a retry bound impossible to
        // enforce: the step could fail for ever, one attempt at a time.
        QVERIFY2(attempts > previous, "the attempt counter went backwards");
        previous = attempts;

        QVERIFY(task.failStep(id, "still unavailable"));
        const auto decision =
            policy.evaluate(FailureKind::Transient, *task.findStep(id), task.status());
        if (decision.action != Action::Retry) {
            break;
        }
        QVERIFY(task.retryStep(id));
    }

    QCOMPARE(previous, 3);
}

void TestAgentRecovery::retryLimitIsEnforced() {
    RecoveryPolicy policy{{.enabled = true, .maxAttempts = 2, .baseBackoff = {}}};
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "unavailable"));
    QCOMPARE(policy.evaluate(FailureKind::Transient, *task.findStep(id), task.status())
                 .action,
             Action::Retry);

    QVERIFY(task.retryStep(id));
    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "unavailable again"));

    // Two attempts made, two allowed. There is no third.
    QCOMPARE(task.findStep(id)->attempts(), 2);
    QCOMPARE(policy.evaluate(FailureKind::Transient, *task.findStep(id), task.status())
                 .action,
             Action::Skip);
}

void TestAgentRecovery::nonRetryableErrorDoesNotRetry_data() {
    QTest::addColumn<int>("kind");

    QTest::newRow("permanent") << static_cast<int>(FailureKind::Permanent);
    QTest::newRow("denied") << static_cast<int>(FailureKind::Denied);
    QTest::newRow("confirmation refused")
        << static_cast<int>(FailureKind::ConfirmationRefused);
    QTest::newRow("cancelled") << static_cast<int>(FailureKind::Cancelled);
    QTest::newRow("context exceeded")
        << static_cast<int>(FailureKind::ContextExceeded);
}

void TestAgentRecovery::nonRetryableErrorDoesNotRetry() {
    QFETCH(int, kind);

    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "no"));

    const auto decision = policy.evaluate(static_cast<FailureKind>(kind),
                                          *task.findStep(id), task.status());
    QVERIFY2(decision.action != Action::Retry,
             qPrintable(QStringLiteral("retried a %1 failure")
                            .arg(QString::fromUtf8(
                                failureKindKey(static_cast<FailureKind>(kind)).data()))));
}

// ---------------------------------------------------------------------------
// The safety rules
// ---------------------------------------------------------------------------

void TestAgentRecovery::destructiveStepCannotAutomaticallyRetry() {
    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::Destructive, "destroy_thing");
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "the tool failed somewhere"));

    // Transient - the most retryable kind there is. It still must not repeat:
    // when it is unknown whether the effect happened, doing it again on a guess
    // is worse than stopping.
    const auto decision =
        policy.evaluate(FailureKind::Transient, *task.findStep(id), task.status());
    QCOMPARE(decision.action, Action::Fail);
    QVERIFY(QString::fromStdString(decision.reason)
                .contains(QStringLiteral("irreversible")));
}

void TestAgentRecovery::confirmRequiredStepCannotAutomaticallyRetry() {
    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::ConfirmRequired, "confirm_thing");
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "the tool failed"));

    // The user approved one attempt, not however many the agent decides to
    // make. Retrying silently would spend an approval twice.
    const auto decision =
        policy.evaluate(FailureKind::Transient, *task.findStep(id), task.status());
    QCOMPARE(decision.action, Action::Fail);
    QVERIFY(QString::fromStdString(decision.reason)
                .contains(QStringLiteral("without asking again")));
}

void TestAgentRecovery::confirmationRefusalIsFinal() {
    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::ConfirmRequired, "confirm_thing");
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "declined"));

    // Asking again because the first answer was not the one the agent wanted is
    // not recovery.
    QCOMPARE(policy.evaluate(FailureKind::ConfirmationRefused, *task.findStep(id),
                             task.status())
                 .action,
             Action::Fail);
}

void TestAgentRecovery::deniedActionIsNeverRetried() {
    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "denied"));

    QCOMPARE(policy.evaluate(FailureKind::Denied, *task.findStep(id), task.status())
                 .action,
             Action::Fail);
}

void TestAgentRecovery::cancelledTaskCannotRetry() {
    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.cancel());

    // Even a transient failure arriving now must not restart anything: the user
    // stopped it.
    const auto decision =
        policy.evaluate(FailureKind::Transient, *task.findStep(id), task.status());
    QVERIFY(decision.action != Action::Retry);
}

void TestAgentRecovery::aFinishedTaskCannotGainWork() {
    RecoveryPolicy policy;

    for (const TaskStatus status :
         {TaskStatus::Completed, TaskStatus::Cancelled, TaskStatus::Failed}) {
        Task task = taskWith(tools::PermissionLevel::ReadOnly);
        const StepId id = task.steps()[0].id();
        QVERIFY(task.beginStep(id));
        QVERIFY(task.failStep(id, "unavailable"));

        // A result arriving late from work already abandoned must not restart
        // it. This is the policy's half of the late-callback guarantee.
        const auto decision =
            policy.evaluate(FailureKind::Transient, *task.findStep(id), status);
        QCOMPARE(decision.action, Action::Fail);
    }
}

void TestAgentRecovery::successfulStepCannotRetry() {
    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.completeStep(id));

    // Repeating it would repeat an action the user already saw happen.
    const auto decision =
        policy.evaluate(FailureKind::Transient, *task.findStep(id), task.status());
    QCOMPARE(decision.action, Action::Fail);

    // And the task model refuses independently, so both layers agree.
    QVERIFY(!task.retryStep(id));
}

void TestAgentRecovery::contextOverflowIsNotRetried() {
    RecoveryPolicy policy;
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "too big"));

    // Retrying the same step with the same context fails identically. The
    // context has to shrink first, which is not this step's business.
    QCOMPARE(policy.evaluate(FailureKind::ContextExceeded, *task.findStep(id),
                             task.status())
                 .action,
             Action::Fail);
}

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

void TestAgentRecovery::backoffIsBoundedAndGrows() {
    RecoveryPolicy policy{
        {.enabled = true, .maxAttempts = 5, .baseBackoff = std::chrono::milliseconds{200}}};

    QCOMPARE(policy.backoffFor(1), std::chrono::milliseconds{200});
    QCOMPARE(policy.backoffFor(2), std::chrono::milliseconds{400});
    QCOMPARE(policy.backoffFor(3), std::chrono::milliseconds{800});

    // However far it is pushed, it never grows into a wait indistinguishable
    // from a hang.
    for (int attempt = 1; attempt < 100; ++attempt) {
        QVERIFY(policy.backoffFor(attempt) <= RecoveryPolicy::kMaxBackoff);
    }
}

void TestAgentRecovery::attemptCeilingCannotBeRaised() {
    RecoveryPolicy policy{{.enabled = true, .maxAttempts = 1000, .baseBackoff = {}}};
    QCOMPARE(policy.config().maxAttempts, RecoveryPolicy::kMaxAttemptsCeiling);

    // Narrowing is honoured; zero is not a way to disable the check into
    // something meaningless.
    policy.setConfig({.enabled = true, .maxAttempts = 0, .baseBackoff = {}});
    QCOMPARE(policy.config().maxAttempts, 1);
}

void TestAgentRecovery::disabledRecoverySkipsRatherThanRetries() {
    RecoveryPolicy policy{{.enabled = false, .maxAttempts = 5, .baseBackoff = {}}};
    Task task = taskWith(tools::PermissionLevel::ReadOnly);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "unavailable"));

    // Skip, not Fail: one failed reading does not have to abandon the whole
    // task, it just does not get another go.
    const auto decision =
        policy.evaluate(FailureKind::Transient, *task.findStep(id), task.status());
    QCOMPARE(decision.action, Action::Skip);
}

QTEST_MAIN(TestAgentRecovery)
#include "tst_agent_recovery.moc"
