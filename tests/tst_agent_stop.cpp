// Global Stop.
//
// Stop is the one control a user reaches for when something is going wrong, so
// it has to work from every state and it has to work the first time. These
// tests come at it from twelve directions, and about half of them are really
// about the same question: can work that was already in flight come back later
// and undo the stop?
//
// The answer has to be no even when a *new* task has started in the meantime -
// which is the case that a null check on the current task does not cover, and
// which the generation guard exists for.

#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <vector>

#include "AgentLoop.h"
#include "AiCoreModel.h"
#include "LlmController.h"
#include "ToolCoordinator.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/llm/ILLMBackend.h"
#include "jarvis/system/WindowsSystemMetricsProvider.h"

using namespace jarvis;

namespace {

/// A backend whose generation can be held open, so a stop can land while one is
/// genuinely outstanding rather than only between them.
class ScriptedBackend final : public llm::ILLMBackend {
public:
    std::vector<std::string> replies;
    int generateCalls{0};

    std::string_view name() const noexcept override { return "scripted"; }
    std::vector<llm::DeviceInfo> devices() const override { return {}; }
    bool supportsGpuOffload() const noexcept override { return false; }

    core::Status load(const llm::ModelInfo& model,
                      const llm::LoadParams& params) override {
        m_loaded.info = model;
        m_loaded.params = params;
        m_loaded.contextLength = 4096;
        m_loaded.totalLayers = 1;
        m_loaded.deviceName = "scripted";
        m_loaded.backendName = "scripted";
        m_isLoaded = true;
        return core::ok();
    }

    void unload() override { m_isLoaded = false; }
    bool isLoaded() const noexcept override { return m_isLoaded; }
    const llm::LoadedModel& loadedModel() const override { return m_loaded; }

    core::Result<llm::GenerationStats> generate(
        const llm::GenerationRequest& request,
        const llm::TokenCallback& onToken) override {
        lastRequest = request;

        const int index = generateCalls++;
        const std::string reply =
            index < static_cast<int>(replies.size())
                ? replies[static_cast<std::size_t>(index)]
                : std::string{"Готово."};
        onToken(reply);

        llm::GenerationStats stats;
        stats.generatedTokens = 10;
        stats.promptTokens = 20;
        return stats;
    }

    void requestStop() noexcept override { ++stopRequests; }

    int stopRequests{0};
    llm::GenerationRequest lastRequest;

private:
    llm::LoadedModel m_loaded;
    bool m_isLoaded{false};
};

} // namespace

class TestAgentStop : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void stopDuringPlanning();
    void stopDuringToolExecution();
    void stopDuringConfirmation();
    void stopDuringRetry();
    void stopBeforeGenerationCallback();
    void stopBeforeToolCallback();
    void stopTwiceIsSafe();
    void stopWithNothingRunningIsSafe();
    void stopAfterCompletedDoesNotRewriteOutcome();
    void stopAfterFailedDoesNotRewriteOutcome();
    void lateCallbackCannotResurrectTask();
    void lateCallbackCannotAttachToTheNextTask();
    void noNewToolCallAfterGlobalStop();
    void stopReachesTheModelAndTheTools();

private:
    std::unique_ptr<core::ThreadPool> m_pool;
    std::unique_ptr<system::WindowsSystemMetricsProvider> m_metrics;
    std::unique_ptr<app::AiCoreModel> m_core;
    std::unique_ptr<app::ToolCoordinator> m_coordinator;
    std::unique_ptr<app::LlmController> m_llm;
    std::unique_ptr<app::AgentLoop> m_agent;
    ScriptedBackend* m_backend{nullptr};

    [[nodiscard]] int executedToolCount() const;
};

int TestAgentStop::executedToolCount() const {
    m_coordinator->refreshAudit();
    QAbstractItemModel* audit = m_coordinator->audit();
    int count = 0;
    for (int row = 0; row < audit->rowCount(); ++row) {
        if (audit->data(audit->index(row, 0), app::AuditModel::EventRole).toString()
            == QStringLiteral("EXECUTION_SUCCEEDED")) {
            ++count;
        }
    }
    return count;
}

void TestAgentStop::init() {
    m_pool = std::make_unique<core::ThreadPool>(2);
    m_metrics = std::make_unique<system::WindowsSystemMetricsProvider>();
    m_core = std::make_unique<app::AiCoreModel>();
    m_coordinator =
        std::make_unique<app::ToolCoordinator>(*m_pool, *m_metrics, *m_core);
    m_coordinator->applySettings(config::ToolSettings{});

    auto backend = std::make_unique<ScriptedBackend>();
    m_backend = backend.get();
    m_llm = std::make_unique<app::LlmController>(*m_pool, std::move(backend), *m_core);

    llm::ModelInfo info;
    info.path = "scripted.gguf";
    static_cast<void>(m_backend->load(info, {}));

    m_agent = std::make_unique<app::AgentLoop>(*m_llm, *m_coordinator, *m_core);

    config::AgentSettings settings;
    settings.enabled = true;
    settings.plannerEnabled = true;
    settings.maxRetries = 3;
    m_agent->applySettings(settings);
}

void TestAgentStop::cleanup() {
    m_agent.reset();
    m_llm.reset();
    m_coordinator.reset();
    m_core.reset();
    m_pool->shutdown();
    m_pool.reset();
    m_metrics.reset();
}

// ---------------------------------------------------------------------------
// Stopping from each state
// ---------------------------------------------------------------------------

void TestAgentStop::stopDuringPlanning() {
    // A plan that would run two steps. Stop lands before any of them does.
    m_backend->replies = {
        R"({"type":"plan","steps":[{"tool":"time_info","arguments":{}},)"
        R"({"tool":"device_info","arguments":{}}]})",
        "Готово.",
    };

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("две вещи"));
    m_agent->stop();

    QTest::qWait(400);

    QVERIFY(!m_agent->busy());
    QCOMPARE(m_agent->agentStateKey(), QStringLiteral("IDLE"));
    QCOMPARE(finished.count(), 1);
    QVERIFY(!finished.first().at(1).toBool());
}

void TestAgentStop::stopDuringToolExecution() {
    m_backend->replies = {R"({"tool": "cpu_info", "arguments": {}})", "Готово."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("загрузка процессора"));

    // cpu_info deliberately takes ~120 ms: a load figure is a delta and the
    // first sample has no interval. That gives a real window to stop inside.
    m_agent->stop();
    QTest::qWait(600);

    QVERIFY(!m_agent->busy());
    QCOMPARE(finished.count(), 1);
    QVERIFY(!finished.first().at(1).toBool());
}

void TestAgentStop::stopDuringConfirmation() {
    m_backend->replies = {
        R"({"tool":"open_application","arguments":{"application":"calculator"}})",
        "Готово.",
    };

    QSignalSpy requested{m_coordinator->confirmation(),
                         &app::ConfirmationManager::requested};
    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};

    m_agent->submit(QStringLiteral("открой калькулятор"));
    QVERIFY(!requested.isEmpty() || requested.wait(3000));
    QVERIFY(m_coordinator->confirmation()->pending());

    m_agent->stop();
    QTest::qWait(300);

    // The question goes away with the task. Leaving a dialog behind for a task
    // that no longer exists would be an approval with nothing to approve.
    QVERIFY(!m_coordinator->confirmation()->pending());
    QVERIFY(!m_agent->busy());
    QCOMPARE(finished.count(), 1);
    QCOMPARE(executedToolCount(), 0);
}

void TestAgentStop::stopDuringRetry() {
    // gpu_info fails on a machine with no NVML card; on one with a card it
    // succeeds. Either way the point is the same: stop while the loop is
    // between attempts, and no further attempt may run.
    m_backend->replies = {R"({"tool": "gpu_info", "arguments": {}})", "Готово."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("видеокарта"));

    QTest::qWait(50);
    m_agent->stop();

    // Long enough for any backoff timer to have fired had it survived.
    QTest::qWait(1500);

    QVERIFY(!m_agent->busy());
    QCOMPARE(m_agent->retryCount(), 0);
    QCOMPARE(finished.count(), 1);
}

void TestAgentStop::stopBeforeGenerationCallback() {
    m_backend->replies = {"Ответ, который уже не нужен."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("вопрос"));
    m_agent->stop();

    QTest::qWait(400);

    // Exactly one ending, and it is the stop. The generation's own completion
    // must not produce a second.
    QCOMPARE(finished.count(), 1);
    QVERIFY(!finished.first().at(1).toBool());
}

void TestAgentStop::stopBeforeToolCallback() {
    m_backend->replies = {R"({"tool": "time_info", "arguments": {}})", "Готово."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("который час"));
    m_agent->stop();

    QTest::qWait(600);

    QCOMPARE(finished.count(), 1);
    QVERIFY(!m_agent->busy());
}

void TestAgentStop::stopTwiceIsSafe() {
    m_backend->replies = {R"({"tool": "time_info", "arguments": {}})", "Готово."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("который час"));

    m_agent->stop();
    m_agent->stop();
    m_agent->stop();

    QTest::qWait(400);

    // Idempotent: pressing Stop three times reports one ending, not three.
    QCOMPARE(finished.count(), 1);
    QVERIFY(!m_agent->busy());
}

void TestAgentStop::stopWithNothingRunningIsSafe() {
    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};

    m_agent->stop();
    m_agent->stop();

    QCOMPARE(finished.count(), 0);
    QVERIFY(!m_agent->busy());
    QCOMPARE(m_agent->agentStateKey(), QStringLiteral("IDLE"));
}

void TestAgentStop::stopAfterCompletedDoesNotRewriteOutcome() {
    m_backend->replies = {"Два плюс два будет четыре."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("сколько будет два плюс два"));
    QVERIFY(!finished.isEmpty() || finished.wait(5000));

    QVERIFY(finished.first().at(1).toBool());   // it succeeded

    m_agent->stop();
    QTest::qWait(200);

    // Stopping afterwards cannot turn a finished answer into a cancellation.
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.first().at(1).toBool());
}

void TestAgentStop::stopAfterFailedDoesNotRewriteOutcome() {
    // An empty reply is reported as a failure, not a silent success.
    m_backend->replies = {"<think>reasoning that never finishes"};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("вопрос"));
    QVERIFY(!finished.isEmpty() || finished.wait(5000));

    QVERIFY(!finished.first().at(1).toBool());
    const QString reason = finished.first().at(0).toString();

    m_agent->stop();
    QTest::qWait(200);

    // The user needs to know it broke, not that they stopped it.
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).toString(), reason);
}

// ---------------------------------------------------------------------------
// Late work
// ---------------------------------------------------------------------------

void TestAgentStop::lateCallbackCannotResurrectTask() {
    m_backend->replies = {R"({"tool": "cpu_info", "arguments": {}})", "Готово."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("процессор"));
    m_agent->stop();

    // cpu_info is still sampling on the pool. Its result lands after the stop.
    QTest::qWait(800);

    QVERIFY(!m_agent->busy());
    QCOMPARE(m_agent->taskId(), QString{});
    QCOMPARE(finished.count(), 1);
    QCOMPARE(m_agent->agentStateKey(), QStringLiteral("IDLE"));
}

void TestAgentStop::lateCallbackCannotAttachToTheNextTask() {
    // The case a null check on the current task does not cover: work is
    // abandoned, a *new* task starts, and only then does the old result arrive.
    // Without a generation guard it would be attributed to the new task.
    m_backend->replies = {
        R"({"tool": "cpu_info", "arguments": {}})",   // first task, slow tool
        "Ответ на второй вопрос.",                     // second task, plain answer
        "Ещё один ответ.",
    };

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};

    m_agent->submit(QStringLiteral("первый вопрос"));
    m_agent->stop();

    // Start a second task immediately, while the first tool is still running.
    const bool started = m_agent->submit(QStringLiteral("второй вопрос"));
    QTest::qWait(900);

    if (started) {
        // Two endings: the stop, then whatever became of the second task. The
        // stray tool result must not have added a third, nor changed either.
        QCOMPARE(finished.count(), 2);
        QVERIFY(!finished.at(0).at(1).toBool());

        const QString second = finished.at(1).at(0).toString();
        if (finished.at(1).at(1).toBool()) {
            // The controller was free, so the second task ran and answered.
            QCOMPARE(second, QStringLiteral("Ответ на второй вопрос."));
        } else {
            // The controller was still winding the first generation down, so
            // the second task failed immediately - and said why. That refusal
            // used to be silent, leaving a task that never started and never
            // ended; it is now visible, which is what this branch asserts.
            QVERIFY2(!second.isEmpty(),
                     "a refused second task ended with nothing to show the user");
        }
    } else {
        // submit() refused outright, because the first task had not been
        // released yet. Also correct - and still exactly one ending.
        QCOMPARE(finished.count(), 1);
    }
    QVERIFY(!m_agent->busy());
}

void TestAgentStop::noNewToolCallAfterGlobalStop() {
    // A plan of several steps. After a stop, none of the remaining steps may
    // run - the loop must not walk on to step two.
    m_backend->replies = {
        R"({"type":"plan","steps":[{"tool":"time_info","arguments":{}},)"
        R"({"tool":"device_info","arguments":{}},)"
        R"({"tool":"time_info","arguments":{}}]})",
        "Готово.",
    };

    m_agent->submit(QStringLiteral("три шага"));
    m_agent->stop();
    QTest::qWait(800);

    // At most the step that was already dispatched when Stop arrived.
    QVERIFY2(executedToolCount() <= 1,
             qPrintable(QStringLiteral("%1 tools ran after Stop")
                            .arg(executedToolCount())));
    QVERIFY(!m_agent->busy());
}

void TestAgentStop::stopReachesTheModelAndTheTools() {
    m_backend->replies = {R"({"tool": "cpu_info", "arguments": {}})", "Готово."};

    m_agent->submit(QStringLiteral("процессор"));
    m_agent->stop();

    // Stop is global: it asks the model to stop generating and tells the tool
    // pipeline to abandon what it is doing. One control, every subsystem.
    QVERIFY2(m_backend->stopRequests > 0, "the model was never asked to stop");

    QTest::qWait(600);
    QVERIFY(!m_agent->busy());
}

QTEST_MAIN(TestAgentStop)
#include "tst_agent_stop.moc"
