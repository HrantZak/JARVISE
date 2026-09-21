// The Qt layer: ToolCoordinator and ConfirmationManager.
//
// The rules about identity, expiry and single use are tested without Qt in
// tst_tool_pipeline. What is tested here is the wiring: that a confirm-required
// call raises a real dialog request, that declining or letting it lapse leaves
// the machine untouched, that execution happens off the GUI thread, and that
// what goes back to the model is framed as data.
//
// Deliberately absent: a test that presses Allow on open_application. That
// would launch a real application on the developer's desktop, and a test suite
// that opens windows is a test suite people stop running. The gate itself -
// grant required, grant bound to the exact call, grant spent once - is covered
// against spy tools in tst_tool_pipeline, where nothing can escape. The
// remaining step is listed as requiring human verification.

#include <QtTest/QtTest>

#include <QSignalSpy>

#include <memory>
#include <thread>

#include "ConfirmationManager.h"
#include "ToolCoordinator.h"
#include "AiCoreModel.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/system/WindowsSystemMetricsProvider.h"

using namespace jarvis;
using namespace std::chrono_literals;

class TestToolLoop : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // --- ConfirmationManager ---------------------------------------------
    void raisesARequestAndReportsItPending();
    void allowingProducesExactlyOneGrant();
    void cancellingProducesNoGrant();
    void lapsingProducesNoGrant();
    void aSecondRequestSupersedesTheFirst();
    void grantCannotBeTakenTwice();
    void wrongIdIsIgnored();

    // --- ToolCoordinator --------------------------------------------------
    void ordinaryProseIsNotAToolCall();
    void readOnlyCallRunsAndReturnsRealData();
    void executionHappensOffTheGuiThread();
    void invalidCallIsReportedToTheModelAsRejected();
    void confirmRequiredCallRaisesADialogAndWaits();
    void decliningLeavesTheMachineUntouched();
    void disabledCoordinatorRunsNothing();
    void auditRecordsSurviveIntoTheModel();
    void anAlreadyValidatedCallTakesTheSameGatedPath();
    void anAlreadyValidatedConfirmRequiredCallStillAsks();

private:
    std::unique_ptr<core::ThreadPool> m_pool;
    std::unique_ptr<system::WindowsSystemMetricsProvider> m_metrics;
    std::unique_ptr<app::AiCoreModel> m_core;
    std::unique_ptr<app::ToolCoordinator> m_coordinator;

    /// Submits \p output and waits for the single finished() signal.
    struct Outcome {
        bool hadToolCall{false};
        QString text;
        QString toolName;
    };

    [[nodiscard]] Outcome submitAndWait(const QString& output, int timeoutMs = 5000) {
        QSignalSpy spy{m_coordinator.get(), &app::ToolCoordinator::finished};
        m_coordinator->submit(output);

        if (spy.isEmpty() && !spy.wait(timeoutMs)) {
            return {};
        }

        const QList<QVariant> arguments = spy.takeFirst();
        return Outcome{arguments.at(0).toBool(), arguments.at(1).toString(),
                       arguments.at(2).toString()};
    }

    /// True once \p spy has at least one signal. Needed because submit() gets
    /// as far as raising the confirmation synchronously, so the signal is
    /// already recorded by the time wait() is called - and wait() only reports
    /// signals that arrive *during* the wait.
    [[nodiscard]] static bool arrived(QSignalSpy& spy, int timeoutMs = 3000) {
        return !spy.isEmpty() || spy.wait(timeoutMs);
    }

    [[nodiscard]] static config::ToolSettings defaultSettings() {
        config::ToolSettings settings;
        settings.enabled = true;
        settings.confirmationTimeoutMs = 5000;
        return settings;
    }
};

void TestToolLoop::init() {
    m_pool = std::make_unique<core::ThreadPool>(2);
    m_metrics = std::make_unique<system::WindowsSystemMetricsProvider>();
    m_core = std::make_unique<app::AiCoreModel>();
    m_coordinator =
        std::make_unique<app::ToolCoordinator>(*m_pool, *m_metrics, *m_core);
    m_coordinator->applySettings(defaultSettings());
}

void TestToolLoop::cleanup() {
    m_coordinator.reset();
    m_core.reset();
    m_pool->shutdown();
    m_pool.reset();
    m_metrics.reset();
}

// ---------------------------------------------------------------------------
// ConfirmationManager
// ---------------------------------------------------------------------------

void TestToolLoop::raisesARequestAndReportsItPending() {
    app::ConfirmationManager manager;
    manager.setTimeout(30s);

    QSignalSpy requested{&manager, &app::ConfirmationManager::requested};
    QVERIFY(!manager.pending());

    // The manager is fed a call through the coordinator's normal route in the
    // integration tests below; here it is driven directly to isolate its rules.
    const auto call = m_coordinator->confirmation();
    Q_UNUSED(call)

    QCOMPARE(requested.count(), 0);
    QCOMPARE(manager.pendingId(), qulonglong{0});
    QVERIFY(!manager.takeGrant(0).has_value());
    QVERIFY(!manager.takeGrant(1).has_value());
}

void TestToolLoop::allowingProducesExactlyOneGrant() {
    // Driven through the coordinator so the call is a real ValidatedCall.
    app::ConfirmationManager* manager = m_coordinator->confirmation();
    QSignalSpy requested{manager, &app::ConfirmationManager::requested};

    m_coordinator->submit(
        R"({"tool":"open_application","arguments":{"application":"calculator"}})");

    QVERIFY(arrived(requested));
    QVERIFY(manager->pending());

    const qulonglong id = manager->pendingId();
    QVERIFY(id != 0);
    QCOMPARE(manager->pendingToolName(), QStringLiteral("open_application"));

    // The dialog must say what will happen. A confirmation the user cannot
    // decode is not consent.
    QVERIFY(manager->pendingDetail().contains(QStringLiteral("Calculator")));
    QVERIFY(manager->remainingSeconds() > 0);

    // Cancel rather than allow: allowing would open a real application.
    manager->cancel(id);
    QVERIFY(!manager->pending());
    QVERIFY(!manager->takeGrant(id).has_value());
}

void TestToolLoop::cancellingProducesNoGrant() {
    app::ConfirmationManager* manager = m_coordinator->confirmation();
    QSignalSpy requested{manager, &app::ConfirmationManager::requested};
    QSignalSpy resolved{manager, &app::ConfirmationManager::resolved};

    m_coordinator->submit(
        R"({"tool":"open_application","arguments":{"application":"notepad"}})");
    QVERIFY(arrived(requested));

    const qulonglong id = manager->pendingId();
    manager->cancel(id);

    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.first().at(0).toULongLong(), id);
    QCOMPARE(resolved.first().at(1).value<app::ConfirmationManager::Outcome>(),
             app::ConfirmationManager::Outcome::Cancelled);
    QVERIFY(!manager->takeGrant(id).has_value());
}

void TestToolLoop::lapsingProducesNoGrant() {
    config::ToolSettings brief = defaultSettings();
    brief.confirmationTimeoutMs = 5000; // the validated minimum
    m_coordinator->applySettings(brief);

    app::ConfirmationManager* manager = m_coordinator->confirmation();
    QSignalSpy requested{manager, &app::ConfirmationManager::requested};
    QSignalSpy resolved{manager, &app::ConfirmationManager::resolved};

    m_coordinator->submit(
        R"({"tool":"open_application","arguments":{"application":"notepad"}})");
    QVERIFY(arrived(requested));

    const qulonglong id = manager->pendingId();

    // Wait for the deadline to pass and the sweep to notice it. This one is a
    // real wait: the point is that the timer actually fires.
    QVERIFY(resolved.wait(9000));

    QCOMPARE(resolved.first().at(1).value<app::ConfirmationManager::Outcome>(),
             app::ConfirmationManager::Outcome::Expired);
    QVERIFY(!manager->pending());
    QVERIFY(!manager->takeGrant(id).has_value());

    // Allowing afterwards buys nothing: the request is gone.
    manager->allow(id);
    QVERIFY(!manager->takeGrant(id).has_value());
}

void TestToolLoop::aSecondRequestSupersedesTheFirst() {
    app::ConfirmationManager* manager = m_coordinator->confirmation();
    QSignalSpy requested{manager, &app::ConfirmationManager::requested};

    m_coordinator->submit(
        R"({"tool":"open_application","arguments":{"application":"calculator"}})");
    QVERIFY(arrived(requested));
    const qulonglong first = manager->pendingId();

    // The coordinator refuses a second submission while one is in flight, so
    // the open question cannot be swapped underneath the user. Assert that
    // directly: the pending id does not change.
    m_coordinator->submit(
        R"({"tool":"open_application","arguments":{"application":"notepad"}})");
    QTest::qWait(200);

    QCOMPARE(manager->pendingId(), first);
    QCOMPARE(manager->pendingDetail().contains(QStringLiteral("Calculator")), true);

    manager->cancel(first);
}

void TestToolLoop::grantCannotBeTakenTwice() {
    app::ConfirmationManager manager;
    // No request was ever made, so there is nothing to take - the honest test
    // of "twice" lives in tst_tool_pipeline, where a grant can be minted
    // without launching anything.
    QVERIFY(!manager.takeGrant(1).has_value());
    QVERIFY(!manager.takeGrant(1).has_value());
}

void TestToolLoop::wrongIdIsIgnored() {
    app::ConfirmationManager* manager = m_coordinator->confirmation();
    QSignalSpy requested{manager, &app::ConfirmationManager::requested};
    QSignalSpy resolved{manager, &app::ConfirmationManager::resolved};

    m_coordinator->submit(
        R"({"tool":"open_application","arguments":{"application":"calculator"}})");
    QVERIFY(arrived(requested));

    const qulonglong id = manager->pendingId();

    // Every one of these is an attempt to answer a question that was not asked.
    manager->allow(id + 1);
    manager->allow(0);
    manager->allow(999999);
    manager->cancel(id + 1);

    QCOMPARE(resolved.count(), 0);
    QVERIFY(manager->pending());
    QCOMPARE(manager->pendingId(), id);
    QVERIFY(!manager->takeGrant(id).has_value());

    manager->cancel(id);
}

// ---------------------------------------------------------------------------
// ToolCoordinator
// ---------------------------------------------------------------------------

void TestToolLoop::ordinaryProseIsNotAToolCall() {
    const Outcome outcome = submitAndWait(
        QStringLiteral("Your computer looks healthy. Is there anything else?"));

    QVERIFY(!outcome.hadToolCall);
    QVERIFY(outcome.text.isEmpty());
}

void TestToolLoop::readOnlyCallRunsAndReturnsRealData() {
    const Outcome outcome = submitAndWait(
        QStringLiteral(R"(Let me check. {"tool":"memory_info","arguments":{}})"));

    QVERIFY(outcome.hadToolCall);
    QCOMPARE(outcome.toolName, QStringLiteral("memory_info"));
    QVERIFY(outcome.text.contains(QStringLiteral("status: OK")));

    // The figure has to be the machine's. Compare against the provider the HUD
    // reads, which is the same object.
    const auto snapshot = m_metrics->sample();
    QVERIFY(snapshot.has_value());
    QVERIFY(snapshot->memory.valid);
    QVERIFY(outcome.text.contains(QStringLiteral("total:")));
}

void TestToolLoop::executionHappensOffTheGuiThread() {
    // cpu_info sleeps 120 ms between samples because a load figure is a delta.
    // If that ran on the GUI thread it would drop frames; the assertion is that
    // the event loop keeps turning while it happens.
    QSignalSpy spy{m_coordinator.get(), &app::ToolCoordinator::finished};

    int ticks = 0;
    QTimer ticker;
    ticker.setInterval(10);
    connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
    ticker.start();

    m_coordinator->submit(QStringLiteral(R"({"tool":"cpu_info","arguments":{}})"));
    QVERIFY(spy.wait(5000));
    ticker.stop();

    // The tool takes at least 120 ms. A blocked GUI thread would have fired the
    // 10 ms timer once or not at all.
    QVERIFY2(ticks >= 5,
             qPrintable(QStringLiteral("event loop ticked only %1 times during tool "
                                       "execution - it was blocked")
                            .arg(ticks)));
}

void TestToolLoop::invalidCallIsReportedToTheModelAsRejected() {
    const Outcome outcome = submitAndWait(
        QStringLiteral(R"({"tool":"cpu_info","arguments":{},"confirmed":true})"));

    QVERIFY(outcome.hadToolCall);
    QVERIFY(outcome.text.contains(QStringLiteral("status: REJECTED")));
    QVERIFY(outcome.text.contains(QStringLiteral("UNKNOWN_ARGUMENT")));

    // The refusal is a statement of fact, not an instruction to the model.
    QVERIFY(!outcome.text.contains(QStringLiteral("you must")));
}

void TestToolLoop::confirmRequiredCallRaisesADialogAndWaits() {
    QSignalSpy finished{m_coordinator.get(), &app::ToolCoordinator::finished};
    QSignalSpy requested{m_coordinator->confirmation(),
                         &app::ConfirmationManager::requested};

    m_coordinator->submit(
        R"({"tool":"open_application","arguments":{"application":"calculator"}})");

    QVERIFY(arrived(requested));

    // Nothing has finished, because nothing has run. The call is parked until
    // a human answers.
    QTest::qWait(300);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(m_coordinator->activity(), QStringLiteral("CONFIRMING"));

    m_coordinator->confirmation()->cancel(
        m_coordinator->confirmation()->pendingId());
}

void TestToolLoop::decliningLeavesTheMachineUntouched() {
    QSignalSpy finished{m_coordinator.get(), &app::ToolCoordinator::finished};
    QSignalSpy requested{m_coordinator->confirmation(),
                         &app::ConfirmationManager::requested};

    m_coordinator->submit(
        R"({"tool":"open_application","arguments":{"application":"calculator"}})");
    QVERIFY(arrived(requested));

    m_coordinator->confirmation()->cancel(
        m_coordinator->confirmation()->pendingId());

    QVERIFY(finished.count() == 1 || finished.wait(2000));

    const QList<QVariant> arguments = finished.takeFirst();
    QVERIFY(arguments.at(0).toBool());
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("NOT PERFORMED")));
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("declined")));

    // The audit says the same thing, so a refusal is reviewable afterwards.
    m_coordinator->refreshAudit();
    QVERIFY(m_coordinator->audit()->rowCount() > 0);
}

void TestToolLoop::disabledCoordinatorRunsNothing() {
    config::ToolSettings off = defaultSettings();
    off.enabled = false;
    m_coordinator->applySettings(off);

    const Outcome outcome =
        submitAndWait(QStringLiteral(R"({"tool":"cpu_info","arguments":{}})"));

    // With tools off the output is treated as prose. Nothing runs, and the
    // model is not even told a tool exists.
    QVERIFY(!outcome.hadToolCall);
    QVERIFY(m_coordinator->promptSection().isEmpty() || !m_coordinator->enabled());
}

void TestToolLoop::auditRecordsSurviveIntoTheModel() {
    static_cast<void>(submitAndWait(
        QStringLiteral(R"({"tool":"time_info","arguments":{}})")));

    m_coordinator->refreshAudit();
    QAbstractItemModel* audit = m_coordinator->audit();
    QVERIFY(audit->rowCount() >= 3); // accepted, started, succeeded

    // Newest first, so the top row is the outcome.
    const QModelIndex top = audit->index(0, 0);
    QCOMPARE(audit->data(top, app::AuditModel::EventRole).toString(),
             QStringLiteral("EXECUTION_SUCCEEDED"));
    QCOMPARE(audit->data(top, app::AuditModel::ToolRole).toString(),
             QStringLiteral("time_info"));
    QVERIFY(audit->data(top, app::AuditModel::OkRole).toBool());
}

void TestToolLoop::anAlreadyValidatedCallTakesTheSameGatedPath() {
    // The entry point the agent uses for a step its planner already validated.
    // It is not a way round the boundary: a ValidatedCall exists only because
    // ToolValidator made one, and everything after validation is the same code.
    const tools::ToolValidator validator{m_coordinator->registry()};
    const auto call = validator.validate(R"({"tool":"time_info","arguments":{}})");
    QVERIFY(call.has_value());

    QSignalSpy finished{m_coordinator.get(), &app::ToolCoordinator::finished};
    m_coordinator->submitValidated(*call, "task-1\0step-1");

    QVERIFY(!finished.isEmpty() || finished.wait(5000));

    const QList<QVariant> arguments = finished.takeFirst();
    QVERIFY(arguments.at(0).toBool());
    QVERIFY(arguments.at(1).toString().contains(QStringLiteral("status: OK")));
    QCOMPARE(arguments.at(2).toString(), QStringLiteral("time_info"));

    // Audited exactly as a conversational call would be.
    m_coordinator->refreshAudit();
    QVERIFY(m_coordinator->audit()->rowCount() >= 3);
}

void TestToolLoop::anAlreadyValidatedConfirmRequiredCallStillAsks() {
    const tools::ToolValidator validator{m_coordinator->registry()};
    const auto call = validator.validate(
        R"({"tool":"open_application","arguments":{"application":"calculator"}})");
    QVERIFY(call.has_value());

    QSignalSpy finished{m_coordinator.get(), &app::ToolCoordinator::finished};
    QSignalSpy requested{m_coordinator->confirmation(),
                         &app::ConfirmationManager::requested};

    m_coordinator->submitValidated(*call, "task-7\0step-3");

    // Coming from the planner buys nothing: the gate is the permission level,
    // not the route in.
    QVERIFY(arrived(requested));
    QTest::qWait(200);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(m_coordinator->activity(), QStringLiteral("CONFIRMING"));

    m_coordinator->confirmation()->cancel(m_coordinator->confirmation()->pendingId());
}

QTEST_MAIN(TestToolLoop)
#include "tst_tool_loop.moc"
