// The whole thing, with a real model.
//
// Qwen3 is loaded, asked a question about this computer, and its output is put
// through the real coordinator. What comes back has to match what the operating
// system reports — not because the model is trusted to be accurate, but because
// the number never passes through the model at all: it is read by C++, inserted
// into the conversation as data, and the model's only job is to say it back.
//
// Skips cleanly when there is no GGUF model on the machine.
//
// Also measures the pipeline, so the benchmark figures in docs/PHASE5.md are
// taken from a run rather than estimated.

#include <QtTest/QtTest>

#include <QFile>
#include <QSignalSpy>

#include <algorithm>
#include <chrono>
#include <memory>

#include "AiCoreModel.h"
#include "LlmController.h"
#include "AgentLoop.h"
#include "ToolCoordinator.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/llm/LlamaCppBackend.h"
#include "jarvis/llm/ModelRegistry.h"
#include "jarvis/system/WindowsSystemMetricsProvider.h"
#include "jarvis/tools/SystemTools.h"
#include "jarvis/tools/ToolValidator.h"

using namespace jarvis;
using namespace std::chrono_literals;

class TestToolLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void modelAsksForMemoryAndReportsTheRealFigure();
    void modelAsksForTheGraphicsCard();
    void modelAsksForTheOperatingSystem();
    void ordinaryQuestionUsesNoTool();
    void benchmarkTheStages();

private:
    std::unique_ptr<core::ThreadPool> m_pool;
    std::unique_ptr<system::WindowsSystemMetricsProvider> m_metrics;
    std::unique_ptr<app::AiCoreModel> m_core;
    std::unique_ptr<app::ToolCoordinator> m_coordinator;
    std::unique_ptr<app::LlmController> m_llm;
    std::unique_ptr<app::AgentLoop> m_agent;

    bool m_ready{false};

    /// Sends \p question and waits for the exchange to end, tools and all.
    [[nodiscard]] QString ask(const QString& question, int timeoutMs = 180000) {
        QSignalSpy completed{m_agent.get(), &app::AgentLoop::finished};
        m_llm->send(question);

        // Recorded rather than folded into the return value: an exchange that
        // never ended and one that ended with nothing to say are different
        // faults, and an empty QString cannot tell them apart.
        m_answered = !completed.isEmpty() || completed.wait(timeoutMs);
        if (!m_answered) {
            return {};
        }
        return completed.takeFirst().at(0).toString();
    }

    /// False when the last ask() timed out instead of completing.
    bool m_answered{false};

    /// Why a live exchange did not produce what was asked for.
    ///
    /// A live test that fails says only "the answer was wrong", and that covers
    /// six different faults with six different owners. Naming them keeps a
    /// spent reasoning budget from being read as a broken pipeline, and - the
    /// direction that matters more - keeps a broken pipeline from being waved
    /// away as the model having a bad day.
    ///
    /// The classification is evidence-based: it reads the token counts, the
    /// audit log and the visible answer. It never guesses.
    enum class Failure {
        None,
        ModelBudgetExhausted,   ///< the reply ended at the token ceiling
        EmptyVisibleResponse,   ///< finished under budget with nothing to show
        ToolSelectionFailure,   ///< a valid call, but not for the right tool
        InvalidToolCall,        ///< the model's call was rejected as malformed
        SecurityFailure,        ///< something ran that the gates should have stopped
        PipelineFailure,        ///< the exchange never completed at all
    };

    [[nodiscard]] static QString failureName(Failure failure) {
        switch (failure) {
            case Failure::None: return QStringLiteral("NONE");
            case Failure::ModelBudgetExhausted:
                return QStringLiteral("MODEL_BUDGET_EXHAUSTED");
            case Failure::EmptyVisibleResponse:
                return QStringLiteral("EMPTY_VISIBLE_RESPONSE");
            case Failure::ToolSelectionFailure:
                return QStringLiteral("TOOL_SELECTION_FAILURE");
            case Failure::InvalidToolCall: return QStringLiteral("INVALID_TOOL_CALL");
            case Failure::SecurityFailure: return QStringLiteral("SECURITY_FAILURE");
            case Failure::PipelineFailure: return QStringLiteral("PIPELINE_FAILURE");
        }
        return QStringLiteral("UNKNOWN");
    }

    /// Classifies the exchange that just ended.
    ///
    /// \p expectedTools is empty when the question needed no tool. \p answered
    /// is false when ask() timed out rather than returning.
    [[nodiscard]] Failure classify(bool answered, const QString& answer,
                                   const QStringList& expectedTools) const {
        if (!answered) {
            return Failure::PipelineFailure;
        }

        // Anything the gates stopped, or anything that ran without being asked
        // for, outranks every other reading: it is the only class that means
        // the boundary itself moved.
        const QStringList ran = executedTools();
        if (auditContains(QStringLiteral("PERMISSION_DENIED"))
            || auditContains(QStringLiteral("EXECUTION_REFUSED"))) {
            return Failure::SecurityFailure;
        }
        if (expectedTools.isEmpty() && !ran.isEmpty()) {
            return Failure::SecurityFailure;
        }

        if (answer.trimmed().isEmpty()) {
            // The distinction the Phase 6 work turned on: a reply that stopped
            // because it hit the ceiling is a budget to raise, and one that
            // stopped short of it is a pipeline that lost the text.
            const int generated = m_llm->lastGeneratedTokens();
            const int budget = m_llm->lastTokenBudget();
            return (budget > 0 && generated >= budget) ? Failure::ModelBudgetExhausted
                                                       : Failure::EmptyVisibleResponse;
        }

        if (auditContains(QStringLiteral("VALIDATION_REJECTED"))
            || auditContains(QStringLiteral("UNKNOWN_TOOL"))) {
            return Failure::InvalidToolCall;
        }

        for (const QString& expected : expectedTools) {
            if (ran.contains(expected)) {
                return Failure::None;
            }
        }
        return expectedTools.isEmpty() ? Failure::None : Failure::ToolSelectionFailure;
    }

    /// Fails the test with the class named, or passes when there is no failure.
    void requireNoFailure(bool answered, const QString& answer,
                          const QStringList& expectedTools, const QString& label) {
        const Failure failure = classify(answered, answer, expectedTools);
        if (failure == Failure::None) {
            return;
        }

        const QString detail =
            QStringLiteral("%1: %2\n  expected tool(s): %3\n  tools run: %4"
                           "\n  tokens: %5/%6\n  answer: %7")
                .arg(label, failureName(failure),
                     expectedTools.isEmpty() ? QStringLiteral("(none)")
                                             : expectedTools.join(QStringLiteral(", ")),
                     executedTools().isEmpty() ? QStringLiteral("(none)")
                                               : executedTools().join(QStringLiteral(", ")))
                .arg(m_llm->lastGeneratedTokens())
                .arg(m_llm->lastTokenBudget())
                .arg(answer.isEmpty() ? QStringLiteral("(empty)") : answer);

        record(QStringLiteral("FAILURE"), detail);
        QFAIL(qPrintable(detail));
    }

    /// True when the audit log holds an event of this kind.
    [[nodiscard]] bool auditContains(const QString& event) const {
        QAbstractItemModel* audit = m_coordinator->audit();
        for (int row = 0; row < audit->rowCount(); ++row) {
            if (audit->data(audit->index(row, 0), app::AuditModel::EventRole).toString()
                == event) {
                return true;
            }
        }
        return false;
    }

    /// Which tools were executed during the exchange, from the audit log.
    [[nodiscard]] QStringList executedTools() const {
        QStringList names;
        QAbstractItemModel* audit = m_coordinator->audit();
        for (int row = 0; row < audit->rowCount(); ++row) {
            const QModelIndex index = audit->index(row, 0);
            if (audit->data(index, app::AuditModel::EventRole).toString()
                == QStringLiteral("EXECUTION_SUCCEEDED")) {
                names << audit->data(index, app::AuditModel::ToolRole).toString();
            }
        }
        return names;
    }

    void record(const QString& label, const QString& text) {
        // Written to a file rather than the console: the Windows console
        // mangles Cyrillic, and an unreadable transcript is not evidence.
        QFile file{QStringLiteral("tool-live-answers.txt")};
        if (!file.open(QIODevice::Append | QIODevice::WriteOnly)) {
            return;
        }
        file.write(label.toUtf8());
        file.write(": ");
        file.write(text.toUtf8());
        file.write("\n\n");
    }
};

void TestToolLive::initTestCase() {
    llm::ModelRegistry::Options options;
    options.includeOllamaBlobs = true;
    llm::ModelRegistry registry{options};

    std::vector<llm::ModelInfo> models = registry.scan();
    if (models.empty()) {
        QSKIP("no GGUF language model on this machine - the tool loop cannot be "
              "tested end to end");
    }

    m_pool = std::make_unique<core::ThreadPool>(4);
    m_metrics = std::make_unique<system::WindowsSystemMetricsProvider>();
    m_core = std::make_unique<app::AiCoreModel>();
    m_coordinator =
        std::make_unique<app::ToolCoordinator>(*m_pool, *m_metrics, *m_core);

    config::ToolSettings settings;
    settings.enabled = true;
    settings.maxRounds = 4;
    m_coordinator->applySettings(settings);

    m_llm = std::make_unique<app::LlmController>(
        *m_pool, std::make_unique<llm::LlamaCppBackend>(), *m_core);
    m_agent = std::make_unique<app::AgentLoop>(*m_llm, *m_coordinator, *m_core);
    config::AgentSettings agentSettings;
    agentSettings.enabled = true;
    m_agent->applySettings(agentSettings);

    config::LlmSettings llmSettings;
    llmSettings.gpuLayers = -1;
    llmSettings.contextLength = 4096;
    // Qwen3 reasons before it answers, and the reasoning is discarded rather
    // than shown. At 320 tokens it regularly spent the whole budget inside a
    // think block and emitted nothing visible - measured repeatedly on the GPU
    // question, which makes it think longest. That tested the budget, not the
    // pipeline.
    //
    // This raises the room to finish. It does not relax a single assertion: the
    // test still requires the right tool and the machine's real figures.
    llmSettings.maxTokens = 768;
    llmSettings.temperature = 0.3F; // steadier tool calls than the 0.7 default
    m_llm->applySettings(llmSettings);
    m_llm->setLanguage(i18n::Language::Russian);

    std::ranges::sort(models, [](const llm::ModelInfo& a, const llm::ModelInfo& b) {
        return a.fileSizeBytes > b.fileSizeBytes;
    });

    QSignalSpy loaded{m_llm.get(), &app::LlmController::modelLoaded};
    m_llm->loadModel(QString::fromStdString(models.front().path.string()));

    QVERIFY2(loaded.wait(600000), "the model did not load");
    QVERIFY(m_llm->loaded());

    m_ready = true;
    qInfo("model : %s", qPrintable(m_llm->loadedModelName()));
    qInfo("layers: %d/%d on GPU", m_llm->offloadedLayers(), m_llm->totalLayers());
}

void TestToolLive::cleanupTestCase() {
    m_agent.reset();
    m_llm.reset();
    m_coordinator.reset();
    m_core.reset();
    if (m_pool) {
        m_pool->shutdown();
    }
}

void TestToolLive::modelAsksForMemoryAndReportsTheRealFigure() {
    if (!m_ready) {
        QSKIP("no model loaded");
    }
    m_coordinator->clearAudit();
    m_llm->clearConversation();

    const QString answer = ask(QStringLiteral("Сколько у меня оперативной памяти?"));
    record(QStringLiteral("RAM"), answer);

    // Named first, so a failure says which of the six faults this was before
    // the assertions below say what was wrong with the text.
    requireNoFailure(m_answered, answer, {QStringLiteral("memory_info")},
                     QStringLiteral("RAM"));

    QVERIFY2(!answer.isEmpty(), "the exchange produced no answer");
    QVERIFY2(executedTools().contains(QStringLiteral("memory_info")),
             qPrintable(QStringLiteral("memory_info was not used; tools run: %1")
                            .arg(executedTools().join(", "))));

    // The figure has to be the machine's. Compare the whole-gigabyte part
    // against what the provider reports: the model may round or reword, but it
    // cannot invent a different computer, because it never saw one.
    const auto snapshot = m_metrics->sample();
    QVERIFY(snapshot.has_value());
    QVERIFY(snapshot->memory.valid);

    const auto gigabytes = static_cast<int>(
        static_cast<double>(snapshot->memory.totalBytes) / (1024.0 * 1024.0 * 1024.0));

    // 31.89 GB may be reported as "31" or "32"; both are the same reading.
    const bool mentionsTheRealSize =
        answer.contains(QString::number(gigabytes))
        || answer.contains(QString::number(gigabytes + 1));

    QVERIFY2(mentionsTheRealSize,
             qPrintable(QStringLiteral("answer does not contain the real size (%1 GB): %2")
                            .arg(gigabytes)
                            .arg(answer)));
}

void TestToolLive::modelAsksForTheGraphicsCard() {
    if (!m_ready) {
        QSKIP("no model loaded");
    }
    m_coordinator->clearAudit();
    m_llm->clearConversation();

    const QString answer = ask(QStringLiteral("Какая у меня видеокарта?"));
    record(QStringLiteral("GPU"), answer);

    const system::HardwareProfile& profile = m_metrics->profile();
    if (!profile.gpuAvailable) {
        QSKIP("no NVML-capable GPU on this machine");
    }

    requireNoFailure(m_answered, answer,
                     {QStringLiteral("gpu_info"), QStringLiteral("system_info")},
                     QStringLiteral("GPU"));
    QVERIFY(!answer.isEmpty());

    const QStringList used = executedTools();
    QVERIFY2(used.contains(QStringLiteral("gpu_info"))
                 || used.contains(QStringLiteral("system_info")),
             qPrintable(QStringLiteral("no GPU tool was used; tools run: %1")
                            .arg(used.join(", "))));

    // The model name it reports has to be the card that is in this machine.
    // Match on the distinctive part rather than the whole string, since the
    // model may drop "NVIDIA GeForce".
    const QString gpuName = QString::fromStdString(profile.gpuName);
    const QStringList words = gpuName.split(' ', Qt::SkipEmptyParts);
    const QString distinctive = words.isEmpty() ? gpuName : words.last();

    QVERIFY2(answer.contains(distinctive, Qt::CaseInsensitive),
             qPrintable(QStringLiteral("answer does not name the real card (%1): %2")
                            .arg(gpuName, answer)));
}

void TestToolLive::modelAsksForTheOperatingSystem() {
    if (!m_ready) {
        QSKIP("no model loaded");
    }
    m_coordinator->clearAudit();
    m_llm->clearConversation();

    const QString answer = ask(QStringLiteral("Какая у меня операционная система?"));
    record(QStringLiteral("OS"), answer);

    requireNoFailure(m_answered, answer,
                     {QStringLiteral("system_info"), QStringLiteral("device_info")},
                     QStringLiteral("OS"));
    QVERIFY(!answer.isEmpty());

    const QStringList used = executedTools();
    QVERIFY2(used.contains(QStringLiteral("system_info"))
                 || used.contains(QStringLiteral("device_info")),
             qPrintable(QStringLiteral("no system tool was used; tools run: %1")
                            .arg(used.join(", "))));

    // Windows 11, from the corrected reading - not the "Windows 10" the
    // registry still claims.
    QVERIFY2(answer.contains(QStringLiteral("11")),
             qPrintable(QStringLiteral("answer does not name Windows 11: %1").arg(answer)));
}

void TestToolLive::ordinaryQuestionUsesNoTool() {
    if (!m_ready) {
        QSKIP("no model loaded");
    }
    m_coordinator->clearAudit();
    m_llm->clearConversation();

    // Having tools available must not turn every exchange into a tool call.
    const QString answer = ask(QStringLiteral("Сколько будет два плюс два?"));
    record(QStringLiteral("ARITHMETIC"), answer);

    // No expected tool: for this question a tool running at all is the fault,
    // and the classifier calls that SECURITY_FAILURE rather than a wrong answer.
    requireNoFailure(m_answered, answer, {}, QStringLiteral("ARITHMETIC"));
    QVERIFY(!answer.isEmpty());
    QVERIFY2(executedTools().isEmpty(),
             qPrintable(QStringLiteral("a tool ran for a question that needed none: %1")
                            .arg(executedTools().join(", "))));
}

void TestToolLive::benchmarkTheStages() {
    if (!m_ready) {
        QSKIP("no model loaded");
    }

    // Validation, on a call the model might plausibly emit.
    const std::string json = R"({"tool":"memory_info","arguments":{}})";

    tools::ToolRegistry registry;
    for (auto& tool : tools::SystemToolFactory::createAll(*m_metrics)) {
        static_cast<void>(registry.add(std::move(tool)));
    }
    const tools::ToolValidator validator{registry};

    constexpr int kIterations = 2000;
    const auto validationStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        static_cast<void>(validator.validate(json));
    }
    const auto validationUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - validationStart)
            .count()
        / static_cast<double>(kIterations);

    // A read-only tool, through the coordinator, on the pool.
    m_coordinator->clearAudit();
    const auto executionStart = std::chrono::steady_clock::now();
    {
        QSignalSpy finished{m_coordinator.get(), &app::ToolCoordinator::finished};
        m_coordinator->submit(QString::fromStdString(json));
        QVERIFY(!finished.isEmpty() || finished.wait(10000));
    }
    const auto executionMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - executionStart)
                                 .count();

    // The full round trip: question in, answer out, tool round included.
    m_llm->clearConversation();
    const auto roundTripStart = std::chrono::steady_clock::now();
    const QString answer = ask(QStringLiteral("Сколько у меня оперативной памяти?"));
    const auto roundTripMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - roundTripStart)
                                 .count();

    QVERIFY(!answer.isEmpty());

    qInfo("validation      : %.1f us per call", validationUs);
    qInfo("memory_info     : %lld ms end to end through the coordinator",
          static_cast<long long>(executionMs));
    qInfo("full round trip : %lld ms (question -> tool -> answer)",
          static_cast<long long>(roundTripMs));
    qInfo("last generation : %s", qPrintable(m_llm->lastStats()));

    record(QStringLiteral("BENCHMARK"),
           QStringLiteral("validation %1 us, memory_info %2 ms, round trip %3 ms, %4")
               .arg(validationUs, 0, 'f', 1)
               .arg(executionMs)
               .arg(roundTripMs)
               .arg(m_llm->lastStats()));
}

QTEST_MAIN(TestToolLive)
#include "tst_tool_live.moc"
