// A spoken request, end to end, with every engine real.
//
//     Piper -> PCM -> VAD -> Whisper -> VoiceController -> AgentLoop
//         -> ToolValidator -> PermissionManager -> ToolExecutor
//         -> AgentLoop -> Piper -> AudioPlayer
//
// tst_voice_integration proves the *engines* work together; tst_voice_agent
// proves the *wiring* is right against a scripted model. This proves the two
// claims that only a real run can support: that a question asked out loud
// reaches the real agent and comes back with a figure read from this machine,
// and that nothing on that path reads a tool call aloud.
//
// The one substitution is the source of the audio: Piper speaks the question,
// because a test process cannot make a sound in a room. That is stated in the
// report and is not claimed to cover a live microphone - a person speaking into
// a real microphone is a manual check, and it lives in
// docs/phase6-human-verification.md.
//
// Skips cleanly when Whisper, Piper or a GGUF model is missing.

#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QMediaDevices>
#include <QSignalSpy>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>

#include "AgentLoop.h"
#include "AiCoreModel.h"
#include "AudioPlayer.h"
#include "LlmController.h"
#include "ToolCoordinator.h"
#include "VoiceController.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/llm/LlamaCppBackend.h"
#include "jarvis/llm/ModelRegistry.h"
#include "jarvis/system/WindowsSystemMetricsProvider.h"
#include "jarvis/voice/PiperTtsBackend.h"
#include "jarvis/voice/WhisperSttBackend.h"

using namespace jarvis;
namespace fs = std::filesystem;

namespace {

/// Real Piper, with a record of every line it was asked to say.
///
/// A decorator rather than a double: the synthesis is the real one, so the
/// timings stay honest, and the record is what lets the test assert on what
/// JARVIS spoke - which is not otherwise observable from outside.
class RecordingTts final : public voice::ITtsBackend {
public:
    explicit RecordingTts(std::unique_ptr<voice::PiperTtsBackend> inner)
        : m_inner{std::move(inner)} {}

    std::string_view name() const noexcept override { return m_inner->name(); }
    bool supports(i18n::Language language) const override {
        return m_inner->supports(language);
    }
    std::string voiceName(i18n::Language language) const override {
        return m_inner->voiceName(language);
    }
    std::vector<i18n::Language> availableLanguages() const override {
        return m_inner->availableLanguages();
    }

    core::Result<voice::TtsResult> synthesize(const voice::TtsRequest& request) override {
        {
            // Written on the pool, read from the GUI thread by the assertions.
            const std::lock_guard<std::mutex> guard{m_mutex};
            m_spoken.push_back(QString::fromStdString(request.text));
        }
        return m_inner->synthesize(request);
    }

    void requestStop() noexcept override { m_inner->requestStop(); }

    void clear() {
        const std::lock_guard<std::mutex> guard{m_mutex};
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
    std::unique_ptr<voice::PiperTtsBackend> m_inner;
    mutable std::mutex m_mutex;
    std::vector<QString> m_spoken;
};

} // namespace

class TestVoiceAgentLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void aSpokenQuestionAboutThisMachineRunsATool();
    void nothingOnThePathReadsAToolCallAloud();
    void aSpokenQuestionThatNeedsNoToolIsStillAnswered();

private:
    static fs::path whisperModel();
    static fs::path piperExecutable();
    static fs::path voicePath(const char* prefix);

    void report(const QString& line);

    /// Speaks \p question with Piper, feeds the samples to the pipeline, and
    /// waits for the agent's task to end. Returns the answer, empty on timeout.
    [[nodiscard]] QString saySomething(const QString& question, int timeoutMs = 240000);

    [[nodiscard]] QStringList executedTools() const;

    std::unique_ptr<core::ThreadPool> m_pool;
    std::unique_ptr<system::WindowsSystemMetricsProvider> m_metrics;
    std::unique_ptr<app::AiCoreModel> m_core;
    std::unique_ptr<app::ToolCoordinator> m_coordinator;
    std::unique_ptr<app::LlmController> m_llm;
    std::unique_ptr<app::AgentLoop> m_agent;
    std::unique_ptr<app::AudioPlayer> m_player;
    std::unique_ptr<voice::WhisperSttBackend> m_stt;
    std::unique_ptr<RecordingTts> m_tts;
    std::unique_ptr<voice::PiperTtsBackend> m_speaker;
    std::unique_ptr<app::VoiceController> m_voice;

    bool m_ready{false};
    QStringList m_report;
};

fs::path TestVoiceAgentLive::whisperModel() {
    for (const char* candidate : {"../../../models/whisper/ggml-large-v3-turbo.bin",
                                  "../../models/whisper/ggml-large-v3-turbo.bin"}) {
        const fs::path path = fs::absolute(candidate);
        if (fs::is_regular_file(path)) {
            return path;
        }
    }
    return {};
}

fs::path TestVoiceAgentLive::piperExecutable() {
    const fs::path path = "C:/Users/hrant/.helolo/voice/piper/piper.exe";
    return fs::is_regular_file(path) ? path : fs::path{};
}

fs::path TestVoiceAgentLive::voicePath(const char* prefix) {
    const fs::path dir = "C:/Users/hrant/.helolo/voice/models";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return {};
    }
    for (const auto& entry : fs::directory_iterator{dir, ec}) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with(prefix) && entry.path().extension() == ".onnx") {
            return entry.path();
        }
    }
    return {};
}

void TestVoiceAgentLive::report(const QString& line) {
    m_report.append(line);
}

QStringList TestVoiceAgentLive::executedTools() const {
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

void TestVoiceAgentLive::initTestCase() {
    if (whisperModel().empty()) {
        QSKIP("Whisper model missing - the spoken path cannot be tested");
    }
    if (piperExecutable().empty() || voicePath("ru_").empty()) {
        QSKIP("Piper or the Russian voice missing - the spoken path cannot be tested");
    }

    llm::ModelRegistry::Options registryOptions;
    registryOptions.includeOllamaBlobs = true;
    llm::ModelRegistry registry{registryOptions};

    std::vector<llm::ModelInfo> models = registry.scan();
    if (models.empty()) {
        QSKIP("no GGUF language model - the spoken path cannot be tested");
    }
    std::ranges::sort(models, [](const llm::ModelInfo& a, const llm::ModelInfo& b) {
        return a.fileSizeBytes > b.fileSizeBytes;
    });

    m_pool = std::make_unique<core::ThreadPool>(4);
    m_metrics = std::make_unique<system::WindowsSystemMetricsProvider>();
    m_core = std::make_unique<app::AiCoreModel>();

    m_coordinator =
        std::make_unique<app::ToolCoordinator>(*m_pool, *m_metrics, *m_core);
    config::ToolSettings toolSettings;
    toolSettings.enabled = true;
    toolSettings.maxRounds = 4;
    m_coordinator->applySettings(toolSettings);

    m_llm = std::make_unique<app::LlmController>(
        *m_pool, std::make_unique<llm::LlamaCppBackend>(), *m_core);

    config::LlmSettings llmSettings;
    llmSettings.gpuLayers = -1;
    llmSettings.contextLength = 4096;
    // The same budget tst_tool_live uses. Qwen3 reasons before it answers and
    // the reasoning is discarded; a smaller budget tests the budget, not the
    // pipeline. It relaxes no assertion below.
    llmSettings.maxTokens = 768;
    llmSettings.temperature = 0.3F;
    m_llm->applySettings(llmSettings);
    m_llm->setLanguage(i18n::Language::Russian);

    m_agent = std::make_unique<app::AgentLoop>(*m_llm, *m_coordinator, *m_core);
    config::AgentSettings agentSettings;
    agentSettings.enabled = true;
    // The single-call path, as the application ships it: one generation for the
    // call, one for the answer. A planner round trip would cost a third.
    agentSettings.plannerEnabled = false;
    m_agent->applySettings(agentSettings);

    QElapsedTimer timer;
    timer.start();

    // --- Whisper, on the GPU beside Qwen3 -----------------------------------
    voice::WhisperSttBackend::Options sttOptions;
    sttOptions.modelPath = whisperModel();
    sttOptions.useGpu = qEnvironmentVariable("JARVIS_STT_CPU") != QStringLiteral("1");
    m_stt = std::make_unique<voice::WhisperSttBackend>(sttOptions);

    const auto sttLoaded = m_stt->load();
    QVERIFY2(sttLoaded.has_value(),
             sttLoaded ? "" : sttLoaded.error().toUserString().c_str());
    m_stt->setLanguage(i18n::Language::Russian);
    report(QStringLiteral("Whisper load             : %1 ms").arg(timer.elapsed()));

    // --- Piper, twice ------------------------------------------------------
    // One instance stands in for the person asking; the other is JARVIS's own
    // voice, wrapped so the test can see what it was asked to say.
    voice::PiperTtsBackend::Options ttsOptions;
    ttsOptions.executable = piperExecutable();
    ttsOptions.voices[i18n::Language::Russian] = voicePath("ru_");
    if (const fs::path en = voicePath("en_"); !en.empty()) {
        ttsOptions.voices[i18n::Language::English] = en;
    }
    m_speaker = std::make_unique<voice::PiperTtsBackend>(ttsOptions);
    m_tts = std::make_unique<RecordingTts>(
        std::make_unique<voice::PiperTtsBackend>(ttsOptions));

    // --- Qwen3 -------------------------------------------------------------
    timer.restart();
    QSignalSpy loaded{m_llm.get(), &app::LlmController::modelLoaded};
    m_llm->loadModel(QString::fromStdString(models.front().path.string()));
    QVERIFY2(loaded.wait(600000), "the model did not load");
    QVERIFY(m_llm->loaded());

    report(QStringLiteral("Qwen3 load               : %1 ms").arg(timer.elapsed()));
    report(QStringLiteral("model                    : %1").arg(m_llm->loadedModelName()));
    report(QStringLiteral("GPU layers               : %1/%2")
               .arg(m_llm->offloadedLayers())
               .arg(m_llm->totalLayers()));

    // Both models resident at once, which is the configuration the application
    // runs in. A failure here is a real one, not a test artefact.
    QVERIFY2(m_llm->offloadedLayers() > 0,
             "Whisper and Qwen3 together left no room on the GPU for the model");

    m_player = std::make_unique<app::AudioPlayer>();

    app::VoiceController::Dependencies deps;
    deps.pool = m_pool.get();
    deps.stt = m_stt.get();
    deps.tts = m_tts.get();
    deps.player = m_player.get();
    deps.llm = m_llm.get();
    deps.agent = m_agent.get();
    deps.core = m_core.get();
    m_voice = std::make_unique<app::VoiceController>(deps);
    m_voice->setLanguage(i18n::Language::Russian);

    m_ready = true;
}

void TestVoiceAgentLive::cleanupTestCase() {
    QFile file{QStringLiteral("voice-agent-live-report.txt")};
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_report.join(QChar{u'\n'}).toUtf8());
        file.write("\n");
    }

    m_voice.reset();
    m_tts.reset();
    m_speaker.reset();
    m_player.reset();
    m_agent.reset();
    if (m_llm) {
        m_llm->unloadModel();
    }
    m_llm.reset();
    if (m_stt) {
        m_stt->unload();
    }
    m_stt.reset();
    m_coordinator.reset();
    m_core.reset();
    if (m_pool) {
        m_pool->shutdown();
    }
    m_pool.reset();
    m_metrics.reset();
}

QString TestVoiceAgentLive::saySomething(const QString& question, int timeoutMs) {
    voice::TtsRequest prompt;
    prompt.text = question.toStdString();
    prompt.language = i18n::Language::Russian;

    QElapsedTimer timer;
    timer.start();
    const core::Result<voice::TtsResult> spoken = m_speaker->synthesize(prompt);
    if (!spoken) {
        return {};
    }
    report(QStringLiteral("question synthesis       : %1 ms for %2 s of audio")
               .arg(timer.elapsed())
               .arg(spoken->durationSeconds(), 0, 'f', 2));

    // Piper produces 22050 Hz; Whisper wants 16000, and float samples rather
    // than 16-bit. Converted here so the buffer enters the pipeline in exactly
    // the shape AudioCapture would deliver it in.
    voice::AudioBuffer audio;
    audio.reserve(spoken->samples.size());
    const double ratio =
        static_cast<double>(spoken->sampleRate) / voice::kSttSampleRate;
    for (std::size_t i = 0;; ++i) {
        const auto source = static_cast<std::size_t>(static_cast<double>(i) * ratio);
        if (source >= spoken->samples.size()) {
            break;
        }
        audio.push_back(static_cast<float>(spoken->samples[source]) / 32768.0F);
    }
    if (audio.empty()) {
        return {};
    }

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};

    timer.restart();
    m_voice->submitUtterance(audio);

    if (finished.isEmpty() && !finished.wait(timeoutMs)) {
        return {};
    }
    report(QStringLiteral("spoken round trip        : %1 ms").arg(timer.elapsed()));

    const QList<QVariant> arguments = finished.takeFirst();
    if (!arguments.at(1).toBool()) {
        report(QStringLiteral("task failed              : %1").arg(arguments.at(0).toString()));
        return {};
    }
    return arguments.at(0).toString();
}

void TestVoiceAgentLive::aSpokenQuestionAboutThisMachineRunsATool() {
    if (!m_ready) {
        QSKIP("the spoken path is not initialised");
    }
    m_coordinator->clearAudit();
    m_llm->clearConversation();
    m_tts->clear();

    const QString answer =
        saySomething(QStringLiteral("Сколько у меня оперативной памяти?"));

    report(QStringLiteral("transcript               : %1").arg(m_voice->lastTranscript()));
    report(QStringLiteral("answer                   : %1").arg(answer));

    // The failure classes are the ones tst_tool_live uses, with the two the
    // spoken path adds. Naming the class in the message is what keeps a spent
    // reasoning budget from being read as a broken microphone path, and a
    // broken microphone path from being waved away as the model's mood.
    QVERIFY2(!m_voice->lastTranscript().isEmpty(),
             "TRANSCRIPTION_FAILURE: Whisper produced no transcript from the "
             "synthesised question");
    QVERIFY2(!answer.isEmpty(),
             qPrintable(QStringLiteral("%1: the spoken exchange produced no answer "
                                       "(tokens %2/%3, tools run: %4)")
                            .arg(m_llm->lastGeneratedTokens() >= m_llm->lastTokenBudget()
                                     ? QStringLiteral("MODEL_BUDGET_EXHAUSTED")
                                     : QStringLiteral("EMPTY_VISIBLE_RESPONSE"))
                            .arg(m_llm->lastGeneratedTokens())
                            .arg(m_llm->lastTokenBudget())
                            .arg(executedTools().join(QStringLiteral(", ")))));

    // The request reached the agent as a task, not through some side channel.
    QVERIFY2(executedTools().contains(QStringLiteral("memory_info")),
             qPrintable(QStringLiteral("TOOL_SELECTION_FAILURE: memory_info was not "
                                       "used; tools run: %1")
                            .arg(executedTools().join(", "))));

    // And the figure is this machine's. It never passed through the model as a
    // number to be guessed: C++ read it and put it in the conversation as data.
    const auto snapshot = m_metrics->sample();
    QVERIFY(snapshot.has_value());
    QVERIFY(snapshot->memory.valid);

    const auto gigabytes = static_cast<int>(
        static_cast<double>(snapshot->memory.totalBytes) / (1024.0 * 1024.0 * 1024.0));

    const bool mentionsTheRealSize = answer.contains(QString::number(gigabytes))
                                     || answer.contains(QString::number(gigabytes + 1));
    QVERIFY2(mentionsTheRealSize,
             qPrintable(QStringLiteral("answer does not contain the real size (%1 GB): %2")
                            .arg(gigabytes)
                            .arg(answer)));
}

void TestVoiceAgentLive::nothingOnThePathReadsAToolCallAloud() {
    if (!m_ready) {
        QSKIP("the spoken path is not initialised");
    }

    // Same exchange as above - a two-generation task whose first generation is
    // a JSON tool call. What follows is the assertion the previous pipeline
    // would have failed: it streamed the model's chunks into synthesis, so the
    // tool call was read out, brace by brace, before the answer existed.
    //
    // Synthesis starts after the task ends and runs on the pool, so the record
    // has to be waited for rather than read straight away.
    QTRY_VERIFY_WITH_TIMEOUT(!m_tts->everything().isEmpty(), 30000);
    const QString said = m_tts->everything();

    QVERIFY2(!said.contains(u'{'), qPrintable("spoke a JSON brace: " + said));
    QVERIFY2(!said.contains(QStringLiteral("memory_info")),
             qPrintable("spoke a tool name: " + said));
    QVERIFY2(!said.contains(QStringLiteral("arguments")),
             qPrintable("spoke a tool argument list: " + said));
    QVERIFY2(!said.contains(QStringLiteral("<think>")),
             qPrintable("spoke the model's reasoning: " + said));

    report(QStringLiteral("spoken aloud             : %1").arg(said.trimmed()));
}

void TestVoiceAgentLive::aSpokenQuestionThatNeedsNoToolIsStillAnswered() {
    if (!m_ready) {
        QSKIP("the spoken path is not initialised");
    }
    m_coordinator->clearAudit();
    m_llm->clearConversation();
    m_tts->clear();

    const QString answer = saySomething(QStringLiteral("Сколько будет два плюс два?"));

    report(QStringLiteral("plain question answer    : %1").arg(answer));

    QVERIFY2(!answer.isEmpty(), "the spoken exchange produced no answer");
    QVERIFY2(executedTools().isEmpty(),
             qPrintable(QStringLiteral("SECURITY_FAILURE: arithmetic used a tool: %1")
                            .arg(executedTools().join(", "))));

    // As above: the answer is handed to Piper on the pool once the task has
    // ended, so this is a wait, not a read.
    QTRY_VERIFY_WITH_TIMEOUT(!m_tts->everything().isEmpty(), 30000);
    report(QStringLiteral("plain question spoken    : %1")
               .arg(m_tts->everything().trimmed()));
}

QTEST_MAIN(TestVoiceAgentLive)

#include "tst_voice_agent_live.moc"
