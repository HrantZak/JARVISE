// The voice pipeline joined to the agent.
//
// Phase 6 moved orchestration out of LlmController and into AgentLoop, and the
// voice pipeline had to follow. What these tests are really checking is that
// speaking is not a second, weaker way in: a spoken request meets the same
// planner, the same validator, the same permission gate and the same
// confirmation dialog as a typed one, in the same order.
//
// The two properties that motivated the change are here explicitly:
//
//   * a tool call is a JSON object, and JARVIS must never read one aloud;
//   * a turn ends when the *task* ends, not when the first generation does -
//     otherwise the microphone reopens while the answer is still being written.
//
// The model is scripted; the tools are the real ones, because a fake boundary
// would prove nothing about a gate.

#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "AgentLoop.h"
#include "AiCoreModel.h"
#include "AudioPlayer.h"
#include "LlmController.h"
#include "ToolCoordinator.h"
#include "VoiceController.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/llm/ILLMBackend.h"
#include "jarvis/system/WindowsSystemMetricsProvider.h"

using namespace jarvis;

namespace {

/// Emits scripted replies, one per generation.
class ScriptedBackend final : public llm::ILLMBackend {
public:
    std::vector<std::string> replies;
    int generateCalls{0};

    /// How long a generation takes. Zero is the honest default, but a task that
    /// finishes in under a millisecond cannot be observed mid-flight: the tests
    /// about stopping would be asserting against a task that had already ended,
    /// and would pass or fail on scheduling luck.
    int delayMs{0};

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
        m_stopRequested = false;

        // Honouring requestStop() is part of the ILLMBackend contract - "stop at
        // the next token boundary" - and a double that ignored it was not just
        // unrealistic, it changed what the test measured: the abandoned
        // generation stayed on the pool, the next request found the controller
        // busy, and a test about starting a new turn after Stop failed for a
        // reason that cannot happen with llama.cpp.
        for (int elapsed = 0; elapsed < delayMs && !m_stopRequested; elapsed += 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        if (m_stopRequested) {
            return core::fail(core::ErrorCode::Cancelled, "stopped");
        }

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

    void requestStop() noexcept override { m_stopRequested = true; }

    llm::GenerationRequest lastRequest;

private:
    llm::LoadedModel m_loaded;
    bool m_isLoaded{false};

    /// Set from the GUI thread, read on the pool.
    std::atomic<bool> m_stopRequested{false};
};

/// Returns whatever the test put in `nextText`.
class ScriptedStt final : public voice::ISttBackend {
public:
    std::string_view name() const noexcept override { return "scripted-stt"; }

    core::Status load() override { m_loaded = true; return core::ok(); }
    void unload() override { m_loaded = false; }
    bool isLoaded() const noexcept override { return m_loaded; }
    std::string modelDescription() const override { return "scripted"; }
    bool isGpuAccelerated() const noexcept override { return false; }

    i18n::Language language() const noexcept override { return m_language; }
    void setLanguage(i18n::Language language) override { m_language = language; }

    core::Result<voice::SttResult> transcribe(const voice::AudioBuffer& audio) override {
        ++calls;
        voice::SttResult result;
        result.text = nextText;
        result.language = m_language;
        result.durationSeconds =
            static_cast<double>(audio.size()) / voice::kSttSampleRate;
        result.processingMs = 1.0;
        return result;
    }

    void requestStop() noexcept override { ++stopRequests; }

    std::string nextText;
    int calls{0};
    int stopRequests{0};

private:
    bool m_loaded{true};
    i18n::Language m_language{i18n::kDefaultLanguage};
};

/// Records every line it is asked to say. That record is the evidence for most
/// of these tests: what JARVIS spoke, and what it did not.
class ScriptedTts final : public voice::ITtsBackend {
public:
    std::string_view name() const noexcept override { return "scripted-tts"; }

    bool supports(i18n::Language) const override { return true; }
    std::string voiceName(i18n::Language) const override { return "scripted-voice"; }

    std::vector<i18n::Language> availableLanguages() const override {
        return {i18n::Language::Russian, i18n::Language::English};
    }

    /// Makes the next synthesis fail, once.
    bool failNext{false};

    core::Result<voice::TtsResult> synthesize(const voice::TtsRequest& request) override {
        {
            // Synthesis runs on the pool; the assertions run on the GUI thread.
            // The record has to be written and read under a lock, and the count
            // has to move at the same instant as the text - a plain `++calls`
            // before the push let a test see "something was spoken" and then
            // read an empty record. That failed in Release only, where the two
            // writes are free to be reordered.
            const std::lock_guard<std::mutex> guard{m_mutex};
            ++m_calls;
            m_spoken.push_back(QString::fromStdString(request.text));
        }

        if (failNext) {
            failNext = false;
            return core::fail(core::ErrorCode::InternalFailure, "scripted TTS failure");
        }

        voice::TtsResult result;
        result.sampleRate = 22050;
        result.channels = 1;
        result.samples.resize(2205);
        for (std::size_t i = 0; i < result.samples.size(); ++i) {
            result.samples[i] = static_cast<std::int16_t>(
                6000.0 * std::sin(static_cast<double>(i) * 0.05));
        }
        return result;
    }

    void requestStop() noexcept override { ++stopRequests; }

    int stopRequests{0};

    [[nodiscard]] int calls() const {
        const std::lock_guard<std::mutex> guard{m_mutex};
        return m_calls;
    }

    void clear() {
        const std::lock_guard<std::mutex> guard{m_mutex};
        m_calls = 0;
        m_spoken.clear();
    }

    [[nodiscard]] QString everything() const {
        const std::lock_guard<std::mutex> guard{m_mutex};
        QString joined;
        for (const QString& line : m_spoken) {
            joined += line;
            joined += u'\n';
        }
        return joined;
    }

private:
    mutable std::mutex m_mutex;
    int m_calls{0};
    std::vector<QString> m_spoken;
};

/// A block loud enough for the pipeline to treat as an utterance.
voice::AudioBuffer speechLike(int milliseconds) {
    const auto count =
        static_cast<std::size_t>(voice::kSttSampleRate) * milliseconds / 1000U;
    voice::AudioBuffer buffer(count);
    for (std::size_t i = 0; i < count; ++i) {
        buffer[i] = 0.3F * std::sin(static_cast<float>(i) * 0.02F);
    }
    return buffer;
}

} // namespace

class TestVoiceAgent : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // --- the path ----------------------------------------------------------
    void aSpokenRequestBecomesATask();
    void theAnswerIsSpoken();
    void anEmptyTranscriptCreatesNoTask();
    void wakeWordGatesRealPipeline() {
        m_voice->setWakeWordRequired(true);
        m_backend->replies = {"Добрый день, сэр."};
        say(QStringLiteral("обычный разговор рядом"));
        QTRY_COMPARE_WITH_TIMEOUT(m_voice->stateKey(), QStringLiteral("IDLE"), 5000);
        QCOMPARE(m_backend->generateCalls, 0);
        QCOMPARE(m_tts->calls(), 0);
        QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
        say(QStringLiteral("Жарвис, привет"));
        QVERIFY(!finished.isEmpty() || finished.wait(5000));
        QCOMPARE(m_backend->generateCalls, 1);
    }

    // --- what must never be spoken ----------------------------------------
    void aToolCallIsNeverSpokenAloud();
    void theTurnEndsWithTheTaskNotTheFirstGeneration();
    void aFailedTaskIsReportedRatherThanSpoken();

    // --- the gate is the same one -----------------------------------------
    void aSpokenRequestMeetsTheSamePermissionGate();
    void aSpokenRequestRaisesTheSameConfirmation();

    // --- stopping ----------------------------------------------------------
    void cancellingAVoiceTurnStopsTheTask();
    void nothingIsSpokenAfterAStop();
    void aDeliberateStopIsNotReportedAsAnError();
    void aNewVoiceTurnStartsCleanlyAfterAStop();

    // --- failures leave nothing stuck --------------------------------------
    void aFailedToolLeavesThePipelineReady();
    void aFailedSynthesisLeavesNoTaskRunning();

    // --- the two entrances do not interfere --------------------------------
    void aTypedTaskIsNeverSpokenAloud();
    void aSpokenTurnFollowsATypedOne();

private:
    void say(const QString& utterance);

    std::unique_ptr<core::ThreadPool> m_pool;
    std::unique_ptr<system::WindowsSystemMetricsProvider> m_metrics;
    std::unique_ptr<app::AiCoreModel> m_core;
    std::unique_ptr<app::ToolCoordinator> m_coordinator;
    std::unique_ptr<app::LlmController> m_llm;
    std::unique_ptr<app::AgentLoop> m_agent;
    std::unique_ptr<app::AudioPlayer> m_player;
    std::unique_ptr<ScriptedStt> m_stt;
    std::unique_ptr<ScriptedTts> m_tts;
    std::unique_ptr<app::VoiceController> m_voice;
    ScriptedBackend* m_backend{nullptr};

    [[nodiscard]] QStringList executedTools() const;
};

void TestVoiceAgent::init() {
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
    settings.plannerEnabled = false;
    settings.maxToolCalls = 8;
    m_agent->applySettings(settings);

    m_player = std::make_unique<app::AudioPlayer>();
    m_stt = std::make_unique<ScriptedStt>();
    m_tts = std::make_unique<ScriptedTts>();

    app::VoiceController::Dependencies deps;
    deps.pool = m_pool.get();
    deps.stt = m_stt.get();
    deps.tts = m_tts.get();
    deps.player = m_player.get();
    deps.llm = m_llm.get();
    deps.agent = m_agent.get();
    deps.core = m_core.get();
    // No capture: the microphone is a device, and these tests are about what
    // happens after a transcript exists. submitUtterance() is the seam.
    m_voice = std::make_unique<app::VoiceController>(deps);
}

void TestVoiceAgent::cleanup() {
    m_voice.reset();
    m_tts.reset();
    m_stt.reset();
    m_player.reset();
    m_agent.reset();
    m_llm.reset();
    m_coordinator.reset();
    m_core.reset();
    m_pool->shutdown();
    m_pool.reset();
    m_metrics.reset();
}

void TestVoiceAgent::say(const QString& utterance) {
    m_stt->nextText = utterance.toStdString();
    m_voice->submitUtterance(speechLike(1500));
}

QStringList TestVoiceAgent::executedTools() const {
    QStringList names;
    m_coordinator->refreshAudit();
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

// ---------------------------------------------------------------------------
// The path
// ---------------------------------------------------------------------------

void TestVoiceAgent::aSpokenRequestBecomesATask() {
    m_backend->replies = {"Сейчас половина третьего."};
    m_backend->delayMs = 800;  // long enough to look at the task while it runs

    say(QStringLiteral("который час"));

    // The request the agent received is the transcript, unaltered. Voice does
    // not get to phrase things differently on the way in, and it gets a real
    // task with a real identity rather than a side channel.
    QTRY_COMPARE_WITH_TIMEOUT(m_agent->userRequest(), QStringLiteral("который час"),
                              5000);
    QVERIFY(!m_agent->taskId().isEmpty());
}

void TestVoiceAgent::theAnswerIsSpoken() {
    m_backend->replies = {"Сейчас половина третьего."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("который час"));

    QVERIFY(!finished.isEmpty() || finished.wait(10000));
    QTRY_COMPARE_WITH_TIMEOUT(m_tts->calls(), 1, 5000);

    QVERIFY2(m_tts->everything().contains(QStringLiteral("половина третьего")),
             "the finished answer never reached synthesis");
}

void TestVoiceAgent::anEmptyTranscriptCreatesNoTask() {
    // Silence, or speech Whisper could not make out. Submitting it would ask
    // the model to answer a question nobody asked, and would burn a task.
    say(QString{});

    QTRY_COMPARE_WITH_TIMEOUT(m_stt->calls, 1, 5000);
    QTest::qWait(400);

    QCOMPARE(m_backend->generateCalls, 0);
    QCOMPARE(m_agent->taskId(), QString{});
    QCOMPARE(m_tts->calls(), 0);
}

// ---------------------------------------------------------------------------
// What must never be spoken
// ---------------------------------------------------------------------------

void TestVoiceAgent::aToolCallIsNeverSpokenAloud() {
    // The defect this test exists for: the pipeline used to stream the model's
    // chunks straight into synthesis, so the first generation of a tool round -
    // a JSON object - was read out loud, character for character.
    m_backend->replies = {
        R"({"tool": "time_info", "arguments": {}})",
        "Сейчас половина третьего.",
    };

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("который час"));

    QVERIFY(!finished.isEmpty() || finished.wait(10000));
    QTRY_VERIFY_WITH_TIMEOUT(m_tts->calls() > 0, 5000);

    const QString said = m_tts->everything();
    QVERIFY2(!said.contains(u'{'), qPrintable("spoke a JSON brace: " + said));
    QVERIFY2(!said.contains(QStringLiteral("time_info")),
             qPrintable("spoke a tool name: " + said));
    QVERIFY2(!said.contains(QStringLiteral("arguments")),
             qPrintable("spoke a tool argument list: " + said));

    QVERIFY(executedTools().contains(QStringLiteral("time_info")));
    QVERIFY(said.contains(QStringLiteral("половина третьего")));
}

void TestVoiceAgent::theTurnEndsWithTheTaskNotTheFirstGeneration() {
    // Two generations, one task. The turn must end once, at the end - not when
    // the tool round finishes, which would reopen the microphone while the real
    // answer was still being written.
    m_backend->replies = {
        R"({"tool": "time_info", "arguments": {}})",
        "Сейчас половина третьего.",
    };

    QSignalSpy turns{m_voice.get(), &app::VoiceController::turnFinished};
    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};

    say(QStringLiteral("который час"));

    QVERIFY(!finished.isEmpty() || finished.wait(10000));
    QCOMPARE(m_backend->generateCalls, 2);

    QTRY_COMPARE_WITH_TIMEOUT(turns.count(), 1, 10000);
    QTest::qWait(300);
    QCOMPARE(turns.count(), 1);
}

void TestVoiceAgent::aFailedTaskIsReportedRatherThanSpoken() {
    // The model spends its whole budget inside <think> and produces no visible
    // answer. There is nothing to say; the failure has to surface instead of
    // the pipeline going quiet.
    m_backend->replies = {"<think>reasoning that never finishes"};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("вопрос"));

    QVERIFY(!finished.isEmpty() || finished.wait(10000));
    QCOMPARE(finished.takeFirst().at(1).toBool(), false);

    QTRY_VERIFY_WITH_TIMEOUT(!m_voice->lastError().isEmpty(), 5000);
    QCOMPARE(m_tts->calls(), 0);
    QCOMPARE(m_voice->state(), voice::VoicePipelineState::Idle);
}

// ---------------------------------------------------------------------------
// The gate is the same one
// ---------------------------------------------------------------------------

void TestVoiceAgent::aSpokenRequestMeetsTheSamePermissionGate() {
    // close_application needs confirmation; with confirmed actions switched off
    // it is refused. Saying it out loud changes nothing about that.
    config::ToolSettings tools;
    tools.allowConfirmedActions = false;
    m_coordinator->applySettings(tools);

    m_backend->replies = {
        R"({"tool":"close_application","arguments":{"application":"calculator"}})",
        "Это действие запрещено настройками.",
    };

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("закрой калькулятор"));

    QVERIFY(!finished.isEmpty() || finished.wait(10000));

    // The decisive assertion. Nothing ran.
    QVERIFY(executedTools().isEmpty());
}

void TestVoiceAgent::aSpokenRequestRaisesTheSameConfirmation() {
    // With confirmed actions allowed, the dialog appears - it is not skipped
    // because the request arrived through a microphone, and the turn waits for
    // it rather than answering over the top.
    m_backend->replies = {
        R"({"tool":"close_application","arguments":{"application":"calculator"}})",
        "Готово.",
    };

    QSignalSpy requested{m_coordinator->confirmation(),
                         &app::ConfirmationManager::requested};
    say(QStringLiteral("закрой калькулятор"));

    QVERIFY(!requested.isEmpty() || requested.wait(10000));
    QVERIFY(m_agent->busy());
    QCOMPARE(m_tts->calls(), 0);

    // Cancel rather than approve: approving launches a real window.
    m_coordinator->confirmation()->cancel(m_coordinator->confirmation()->pendingId());
    QTest::qWait(300);

    QVERIFY(executedTools().isEmpty());
}

// ---------------------------------------------------------------------------
// Stopping
// ---------------------------------------------------------------------------

void TestVoiceAgent::cancellingAVoiceTurnStopsTheTask() {
    m_backend->replies = {R"({"tool": "time_info", "arguments": {}})", "Готово."};
    m_backend->delayMs = 800;

    say(QStringLiteral("который час"));
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->busy(), 5000);

    m_voice->cancel();

    // Cancelling the generation alone would leave the task alive to start
    // another one. Stop has to be the global one.
    QVERIFY(!m_agent->busy());
    QCOMPARE(m_agent->taskId(), QString{});
}

void TestVoiceAgent::nothingIsSpokenAfterAStop() {
    m_backend->replies = {R"({"tool": "time_info", "arguments": {}})", "Готово."};
    m_backend->delayMs = 600;

    say(QStringLiteral("который час"));
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->busy(), 5000);
    m_voice->cancel();

    // The generation is still running inside the backend and reports back after
    // this wait, which is longer than the delay. A late answer from a task the
    // user stopped must not be spoken.
    QTest::qWait(1600);

    QCOMPARE(m_tts->calls(), 0);
    QVERIFY(!m_agent->busy());
    QCOMPARE(m_voice->state(), voice::VoicePipelineState::Idle);
}

void TestVoiceAgent::aDeliberateStopIsNotReportedAsAnError() {
    // AgentLoop::stop() ends the task and reports the turn as not ok, which is
    // right for the agent: the task did not finish. It is wrong for the person
    // who pressed Stop. Before this was separated, every Stop and every
    // barge-in put "Stopped." in the error banner and flashed the Core red.
    m_backend->replies = {R"({"tool": "time_info", "arguments": {}})", "Готово."};
    m_backend->delayMs = 800;

    say(QStringLiteral("который час"));
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->busy(), 5000);

    m_voice->cancel();

    QVERIFY2(m_voice->lastError().isEmpty(),
             qPrintable(QStringLiteral("stopping reported an error: %1")
                            .arg(m_voice->lastError())));
    QVERIFY(!m_agent->busy());

    // A genuine failure still reports one, or the flag would have turned the
    // error path off altogether.
    m_backend->generateCalls = 0;
    m_backend->delayMs = 0;
    m_backend->replies = {"<think>reasoning that never finishes"};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("вопрос"));
    QVERIFY(!finished.isEmpty() || finished.wait(10000));
    QTRY_VERIFY_WITH_TIMEOUT(!m_voice->lastError().isEmpty(), 5000);
}

void TestVoiceAgent::aNewVoiceTurnStartsCleanlyAfterAStop() {
    m_backend->replies = {R"({"tool": "time_info", "arguments": {}})", "Готово."};
    m_backend->delayMs = 600;

    say(QStringLiteral("который час"));
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->busy(), 5000);
    m_voice->cancel();

    // Stop asks the backend to stop at the next token boundary; the generation
    // then unwinds and the controller reports itself free when the completion
    // lands back on the GUI thread. Until it does, a new request is refused
    // with "The previous request is still finishing" - which is honest, and
    // which a spoken turn never sees, because speaking and transcribing take
    // far longer than the unwind.
    //
    // So the property under test is that the pipeline is not *stuck*: the stage
    // is released, the controller frees itself, and the next turn runs end to
    // end. Waiting on generating() is waiting for that observable state, not
    // sleeping until a problem goes away.
    QVERIFY2(!m_voice->lastError().contains(QStringLiteral("busy")),
             "the stop itself was reported as a busy failure");
    QTRY_VERIFY_WITH_TIMEOUT(!m_llm->generating(), 10000);
    QVERIFY(!m_agent->busy());

    m_backend->generateCalls = 0;
    m_backend->delayMs = 0;
    m_backend->replies = {"Второй ответ на второй вопрос."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("второй вопрос"));

    QVERIFY2(!finished.isEmpty() || finished.wait(10000),
             "no turn started after a stop");

    const QList<QVariant> outcome = finished.takeFirst();
    QVERIFY2(outcome.at(1).toBool(),
             qPrintable(QStringLiteral("the turn after a stop failed: '%1' "
                                       "(voice error: '%2', generations: %3)")
                            .arg(outcome.at(0).toString(), m_voice->lastError())
                            .arg(m_backend->generateCalls)));

    QTRY_COMPARE_WITH_TIMEOUT(m_tts->calls(), 1, 10000);
    QVERIFY(m_tts->everything().contains(QStringLiteral("Второй ответ")));
}

// ---------------------------------------------------------------------------
// Failures leave nothing stuck
// ---------------------------------------------------------------------------

void TestVoiceAgent::aFailedToolLeavesThePipelineReady() {
    // A tool that cannot run - here because the gate refuses it - must end the
    // turn, not hang it. The pipeline has to be ready for the next utterance.
    config::ToolSettings tools;
    tools.allowConfirmedActions = false;
    m_coordinator->applySettings(tools);

    m_backend->replies = {
        R"({"tool":"close_application","arguments":{"application":"calculator"}})",
        "Не разрешено.",
    };

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("закрой калькулятор"));
    QVERIFY(!finished.isEmpty() || finished.wait(10000));

    QTRY_VERIFY_WITH_TIMEOUT(!m_agent->busy(), 5000);
    QVERIFY(executedTools().isEmpty());

    // Ready means: the next utterance produces a task, rather than being
    // dropped by a stage flag nobody cleared.
    // A refusal may itself be spoken after the agent task finishes. Wait for
    // its voice turn to settle before submitting an unrelated next utterance.
    QTRY_COMPARE_WITH_TIMEOUT(m_voice->state(), voice::VoicePipelineState::Idle, 5000);
    m_backend->generateCalls = 0;
    m_backend->replies = {"Ответ на следующий вопрос."};

    QSignalSpy second{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("следующий вопрос"));
    QVERIFY2(!second.isEmpty() || second.wait(10000),
             "the pipeline was still busy after a refused tool");
}

void TestVoiceAgent::aFailedSynthesisLeavesNoTaskRunning() {
    // Synthesis happens after the task has already ended, so a Piper failure
    // cannot strand the agent. Asserted rather than assumed, because the
    // ordering is the only thing that makes it true.
    m_tts->failNext = true;
    m_backend->replies = {"Ответ, который не удастся произнести."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    say(QStringLiteral("скажи что-нибудь"));
    QVERIFY(!finished.isEmpty() || finished.wait(10000));

    QTRY_VERIFY_WITH_TIMEOUT(!m_voice->lastError().isEmpty(), 10000);
    QVERIFY2(!m_agent->busy(), "a synthesis failure left the agent running");
    QCOMPARE(m_agent->taskId(), QString{});
    QVERIFY(m_voice->state() != voice::VoicePipelineState::Error);
}

// ---------------------------------------------------------------------------
// The two entrances do not interfere
// ---------------------------------------------------------------------------

void TestVoiceAgent::aTypedTaskIsNeverSpokenAloud() {
    m_backend->replies = {"Ответ на набранный вопрос."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    QVERIFY(m_agent->submit(QStringLiteral("набранный вопрос")));

    QVERIFY(!finished.isEmpty() || finished.wait(10000));
    QTest::qWait(300);

    // The agent has one `finished` signal for both entrances, so the pipeline
    // has to tell whose turn it is. A typed answer read aloud would be a
    // surprise at best and an eavesdropper's gift at worst.
    QCOMPARE(m_tts->calls(), 0);
    QCOMPARE(m_voice->state(), voice::VoicePipelineState::Idle);
}

void TestVoiceAgent::aSpokenTurnFollowsATypedOne() {
    m_backend->replies = {"Ответ на набранный вопрос.",
                          "Ответ на произнесённый вопрос."};

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    QVERIFY(m_agent->submit(QStringLiteral("набранный вопрос")));
    QVERIFY(!finished.isEmpty() || finished.wait(10000));
    QCOMPARE(m_tts->calls(), 0);

    say(QStringLiteral("произнесённый вопрос"));

    QTRY_COMPARE_WITH_TIMEOUT(m_tts->calls(), 1, 10000);
    QVERIFY(m_tts->everything().contains(QStringLiteral("произнесённый")));
    QVERIFY2(!m_tts->everything().contains(QStringLiteral("набранный")),
             "the previous typed answer leaked into the spoken turn");
}

QTEST_MAIN(TestVoiceAgent)

#include "tst_voice_agent.moc"
