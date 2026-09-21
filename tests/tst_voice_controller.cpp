#include <QSignalSpy>
#include <QSemaphore>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>
#include <mutex>

#include "AiCoreModel.h"
#include "AudioPlayer.h"
#include "LlmController.h"
#include "VoiceController.h"
#include "jarvis/core/ThreadPool.h"

using namespace jarvis;
using namespace jarvis::app;
using namespace jarvis::voice;

namespace {

/// Test double for ISttBackend.
///
/// Doubles are used here and only here: the point of these tests is the
/// orchestration - which stage runs when, and what happens when one fails -
/// and driving that with a 1.5 GB model would test whisper.cpp instead. Real
/// recognition is covered by tst_stt, and the two together are what make the
/// pipeline trustworthy.
class ScriptedStt final : public ISttBackend {
public:
    std::string_view name() const noexcept override { return "scripted-stt"; }

    core::Status load() override { m_loaded = true; return core::ok(); }
    void unload() override { m_loaded = false; }
    bool isLoaded() const noexcept override { return m_loaded; }
    std::string modelDescription() const override { return "scripted"; }
    bool isGpuAccelerated() const noexcept override { return false; }

    i18n::Language language() const noexcept override { return m_language; }
    void setLanguage(i18n::Language language) override { m_language = language; }

    core::Result<SttResult> transcribe(const AudioBuffer& audio) override {
        ++calls;
        lastSampleCount = audio.size();

        if (failNext) {
            return core::fail(core::ErrorCode::InternalFailure, "scripted STT failure");
        }

        SttResult result;
        result.text = nextText;
        result.language = m_language;
        result.durationSeconds = static_cast<double>(audio.size()) / kSttSampleRate;
        result.processingMs = 1.0;
        return result;
    }

    void requestStop() noexcept override { ++stopRequests; }

    std::string nextText;
    bool failNext{false};
    int calls{0};
    int stopRequests{0};
    std::size_t lastSampleCount{0};

private:
    bool m_loaded{true};
    i18n::Language m_language{i18n::kDefaultLanguage};
};

/// Test double for ITtsBackend.
class ScriptedTts final : public ITtsBackend {
public:
    std::string_view name() const noexcept override { return "scripted-tts"; }

    bool supports(i18n::Language) const override { return supported; }
    std::string voiceName(i18n::Language) const override { return "scripted-voice"; }

    std::vector<i18n::Language> availableLanguages() const override {
        return supported ? std::vector<i18n::Language>{i18n::Language::Russian,
                                                       i18n::Language::English}
                         : std::vector<i18n::Language>{};
    }

    core::Result<TtsResult> synthesize(const TtsRequest& request) override {
        ++calls;
        {
            const std::lock_guard lock{spokenMutex};
            spoken.push_back(QString::fromStdString(request.text));
        }
        if (blockSynthesis) synthesisPermits.tryAcquire(1, 5000);

        if (failNext) {
            return core::fail(core::ErrorCode::InternalFailure, "scripted TTS failure");
        }

        // A short, real buffer of actual samples - not silence, so the player
        // has something valid to consume.
        TtsResult result;
        result.sampleRate = 22050;
        result.channels = 1;
        result.samples.resize(4410);
        for (std::size_t i = 0; i < result.samples.size(); ++i) {
            result.samples[i] = static_cast<std::int16_t>(
                6000.0 * std::sin(static_cast<double>(i) * 0.05));
        }
        return result;
    }

    void requestStop() noexcept override { ++stopRequests; }

    bool supported{true};
    bool failNext{false};
    std::atomic<int> calls{0};
    std::atomic<int> stopRequests{0};
    bool blockSynthesis{false};
    QSemaphore synthesisPermits;
    std::mutex spokenMutex;
    std::vector<QString> spoken;
};

/// Each permit emits one sentence, then a third permit completes generation.
/// The test can keep synthesis blocked while the actual streaming controller
/// delivers later chunks and its completion event.
class GatedModel final : public llm::ILLMBackend {
public:
    std::string_view name() const noexcept override { return "gated-model"; }
    std::vector<llm::DeviceInfo> devices() const override { return {}; }
    bool supportsGpuOffload() const noexcept override { return false; }
    core::Status load(const llm::ModelInfo&, const llm::LoadParams&) override {
        return core::ok();
    }
    void unload() override {}
    bool isLoaded() const noexcept override { return true; }
    const llm::LoadedModel& loadedModel() const override { return model; }
    core::Result<llm::GenerationStats> generate(
        const llm::GenerationRequest&, const llm::TokenCallback& onToken) override {
        // The think-tag filter retains six trailing characters. Include that
        // harmless lookahead so each complete sentence reaches the voice queue
        // while the next token is deliberately withheld by this test.
        for (const auto& sentence : {"This is the first response.      ",
                                      " This is the second response.      ", ""}) {
            if (!permits.tryAcquire(1, 5000) || stopped) {
                return core::fail(core::ErrorCode::Cancelled, "stopped");
            }
            // LlmController batches tokens for 33 ms; let each released
            // sentence reach the voice queue before the next permit.
            QThread::msleep(40);
            if (*sentence != '\0' && !onToken(sentence)) {
                return core::fail(core::ErrorCode::Cancelled, "stopped");
            }
        }
        llm::GenerationStats stats;
        stats.generatedTokens = 12;
        return stats;
    }
    void requestStop() noexcept override {
        stopped = true;
        permits.release(3);
    }
    QSemaphore permits;
private:
    std::atomic<bool> stopped{false};
    llm::LoadedModel model = [] {
        llm::LoadedModel value;
        value.contextLength = 4096;
        return value;
    }();
};

/// A block of audio loud enough for the VAD to treat as speech.
AudioBuffer speechLike(int milliseconds) {
    const auto count =
        static_cast<std::size_t>(kSttSampleRate) * milliseconds / 1000U;
    AudioBuffer buffer(count);
    for (std::size_t i = 0; i < count; ++i) {
        buffer[i] = 0.3F * std::sin(static_cast<float>(i) * 0.02F);
    }
    return buffer;
}

} // namespace

/// Orchestration of the voice pipeline.
class TestVoiceController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startsInIdle();
    void reportsUnavailableWithoutBackends();
    void emptyTranscriptNeverReachesTheModel();
    void transcriptIsPublished();
    void sttFailureLandsInASafeState();
    void cancelReturnsToASafeState();
    void cancelStopsTheBackends();
    void destructionDuringWorkIsSafe();
    void languageReachesTheSttBackend();
    void utteranceIsPassedThroughWhole();
    void streamedChunksWaitForSynthesisAndDoNotFinishEarly();
    void cancelledSynthesisCannotPlayItsLateResult();

private:
    VoiceController::Dependencies makeDeps();
    GatedModel* useStreamingModel();

    std::unique_ptr<core::ThreadPool> m_pool;
    std::unique_ptr<ScriptedStt> m_stt;
    std::unique_ptr<ScriptedTts> m_tts;
    std::unique_ptr<AiCoreModel> m_core;
    std::unique_ptr<AudioPlayer> m_player;
    std::unique_ptr<LlmController> m_llm;
    std::unique_ptr<VoiceController> m_voice;
};

VoiceController::Dependencies TestVoiceController::makeDeps() {
    VoiceController::Dependencies deps;
    deps.pool = m_pool.get();
    deps.stt = m_stt.get();
    deps.tts = m_tts.get();
    deps.core = m_core.get();
    deps.player = m_player.get();
    deps.llm = m_llm.get();
    // capture and llm are deliberately absent: these tests drive the pipeline
    // through submitUtterance() and never reach the model.
    return deps;
}

void TestVoiceController::init() {
    m_pool = std::make_unique<core::ThreadPool>(2);
    m_stt = std::make_unique<ScriptedStt>();
    m_tts = std::make_unique<ScriptedTts>();
    m_core = std::make_unique<AiCoreModel>();
    m_player = std::make_unique<AudioPlayer>();
    m_voice = std::make_unique<VoiceController>(makeDeps());
}

void TestVoiceController::cleanup() {
    m_tts->synthesisPermits.release(10);
    m_voice.reset();
    m_llm.reset();
    m_player.reset();
    m_core.reset();
    m_tts.reset();
    m_stt.reset();
    m_pool.reset();
}

void TestVoiceController::startsInIdle() {
    QCOMPARE(m_voice->state(), VoicePipelineState::Idle);
    QCOMPARE(m_voice->stateKey(), QStringLiteral("IDLE"));
    QVERIFY(!m_voice->isEnabled());
    QVERIFY(!m_voice->isListening());
}

void TestVoiceController::reportsUnavailableWithoutBackends() {
    // No microphone was wired in, so the pipeline must say it cannot run and
    // explain why - not fail silently when the user presses the button.
    QVERIFY(!m_voice->isAvailable());
    QVERIFY(!m_voice->unavailableReason().isEmpty());

    QVERIFY(!m_voice->startListening());
    QCOMPARE(m_voice->state(), VoicePipelineState::Unavailable);
    QVERIFY(!m_voice->lastError().isEmpty());
}

void TestVoiceController::emptyTranscriptNeverReachesTheModel() {
    m_stt->nextText = "";  // silence, or speech the model could not make out

    QSignalSpy transcribedSpy{m_voice.get(), &VoiceController::transcribed};

    m_voice->submitUtterance(speechLike(1200));

    QTRY_COMPARE_WITH_TIMEOUT(m_stt->calls, 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(m_voice->state(), VoicePipelineState::Idle, 5000);

    // The decisive assertion: nothing was published, so nothing could have been
    // sent to the model. An empty transcript must not become a prompt.
    QCOMPARE(transcribedSpy.count(), 0);
    QVERIFY(m_voice->lastTranscript().isEmpty());
    QCOMPARE(m_tts->calls.load(), 0);
}

void TestVoiceController::transcriptIsPublished() {
    m_stt->nextText = "Привет, Джарвис";

    QSignalSpy transcribedSpy{m_voice.get(), &VoiceController::transcribed};

    m_voice->submitUtterance(speechLike(1500));

    QVERIFY(transcribedSpy.wait(5000));
    QCOMPARE(transcribedSpy.count(), 1);

    // UTF-8 must survive the worker hop and the queued invocation.
    QCOMPARE(transcribedSpy.at(0).at(0).toString(), QStringLiteral("Привет, Джарвис"));
    QCOMPARE(m_voice->lastTranscript(), QStringLiteral("Привет, Джарвис"));

    // With no model wired in the turn cannot continue, and that is reported as
    // an error rather than a hang.
    QTRY_VERIFY_WITH_TIMEOUT(!m_voice->lastError().isEmpty(), 5000);
    QVERIFY(m_voice->state() == VoicePipelineState::Idle);
}

void TestVoiceController::sttFailureLandsInASafeState() {
    m_stt->failNext = true;

    m_voice->submitUtterance(speechLike(1200));

    QTRY_COMPARE_WITH_TIMEOUT(m_stt->calls, 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_voice->lastError().isEmpty(), 5000);

    // Never stuck in Error: the pipeline falls back somewhere the user can act.
    QTRY_COMPARE_WITH_TIMEOUT(m_voice->state(), VoicePipelineState::Idle, 5000);
    QCOMPARE(m_tts->calls.load(), 0);
}

void TestVoiceController::cancelReturnsToASafeState() {
    m_stt->nextText = "что-то";
    m_voice->submitUtterance(speechLike(1200));

    m_voice->cancel();
    QVERIFY(m_voice->state() == VoicePipelineState::Idle);

    // A late result from the cancelled stage must not resurrect the pipeline.
    QTest::qWait(600);
    QCOMPARE(m_voice->state(), VoicePipelineState::Idle);
}

void TestVoiceController::cancelStopsTheBackends() {
    m_voice->cancel();

    // Cancellation has to reach the engines, or a long transcription would keep
    // running after the user pressed stop.
    QCOMPARE(m_stt->stopRequests, 1);
    QCOMPARE(m_tts->stopRequests.load(), 1);
}

void TestVoiceController::destructionDuringWorkIsSafe() {
    m_stt->nextText = "текст";
    m_voice->submitUtterance(speechLike(2000));

    // Destroy while the worker is mid-flight. The queued completion holds a
    // liveness marker; if that were missing this would be a use-after-free.
    m_voice.reset();

    QTest::qWait(500);
    QVERIFY2(true, "destroying an active VoiceController must not crash");
}

void TestVoiceController::languageReachesTheSttBackend() {
    m_voice->setLanguage(i18n::Language::English);
    QVERIFY(m_stt->language() == i18n::Language::English);

    m_voice->setLanguage(i18n::Language::Russian);
    QVERIFY(m_stt->language() == i18n::Language::Russian);
}

void TestVoiceController::utteranceIsPassedThroughWhole() {
    m_stt->nextText = "ок";

    const AudioBuffer utterance = speechLike(1800);
    m_voice->submitUtterance(utterance);

    QTRY_COMPARE_WITH_TIMEOUT(m_stt->calls, 1, 5000);

    // The recogniser must receive the whole utterance, not a truncated block.
    QCOMPARE(m_stt->lastSampleCount, utterance.size());
}

GatedModel* TestVoiceController::useStreamingModel() {
    m_voice.reset();
    auto backend = std::make_unique<GatedModel>();
    auto* model = backend.get();
    m_llm = std::make_unique<LlmController>(*m_pool, std::move(backend), *m_core);
    m_voice = std::make_unique<VoiceController>(makeDeps());
    m_tts->blockSynthesis = true;
    m_stt->nextText = "Explain the result";
    return model;
}

void TestVoiceController::streamedChunksWaitForSynthesisAndDoNotFinishEarly() {
    auto* model = useStreamingModel();
    QSignalSpy chunks{m_llm.get(), &LlmController::assistantChunk};
    QSignalSpy generationFinished{m_llm.get(), &LlmController::generationCompleted};
    QSignalSpy turnFinished{m_voice.get(), &VoiceController::turnFinished};
    m_voice->submitUtterance(speechLike(500));
    QTRY_COMPARE_WITH_TIMEOUT(m_voice->state(), VoicePipelineState::Thinking, 3000);

    model->permits.release();
    QTRY_COMPARE_WITH_TIMEOUT(m_tts->calls.load(), 1, 3000);
    model->permits.release();
    QTRY_COMPARE_WITH_TIMEOUT(chunks.count(), 2, 3000);
    QTest::qWait(50);
    QCOMPARE(m_tts->calls.load(), 1);

    model->permits.release();
    QTRY_COMPARE_WITH_TIMEOUT(generationFinished.count(), 1, 3000);
    QCOMPARE(m_voice->state(), VoicePipelineState::Synthesizing);
    QCOMPARE(turnFinished.count(), 0);
    m_voice->submitUtterance(speechLike(500));
    QTest::qWait(50);
    QCOMPARE(m_stt->calls, 1);

    m_voice->cancel();
    m_tts->synthesisPermits.release();
    m_pool->waitIdle();
    QCoreApplication::sendPostedEvents();
    QCOMPARE(m_voice->state(), VoicePipelineState::Idle);
    QCOMPARE(m_tts->calls.load(), 1);
}

void TestVoiceController::cancelledSynthesisCannotPlayItsLateResult() {
    auto* model = useStreamingModel();
    QSignalSpy errors{m_voice.get(), &VoiceController::lastErrorChanged};
    QSignalSpy turnFinished{m_voice.get(), &VoiceController::turnFinished};
    m_voice->submitUtterance(speechLike(500));
    QTRY_COMPARE_WITH_TIMEOUT(m_voice->state(), VoicePipelineState::Thinking, 3000);
    model->permits.release();
    QTRY_COMPARE_WITH_TIMEOUT(m_tts->calls.load(), 1, 3000);
    m_voice->cancel();
    QCOMPARE(m_voice->state(), VoicePipelineState::Idle);

    // The double deliberately finishes successfully after Stop. The pipeline
    // must discard this audio regardless of whether a speaker is available.
    m_tts->synthesisPermits.release();
    m_pool->waitIdle();
    QCoreApplication::sendPostedEvents();
    QVERIFY(!m_player->isPlaying());
    QCOMPARE(m_voice->state(), VoicePipelineState::Idle);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(turnFinished.count(), 0);
}

QTEST_MAIN(TestVoiceController)

#include "tst_voice_controller.moc"
