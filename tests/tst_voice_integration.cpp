#include <QElapsedTimer>
#include <QFile>
#include <QMediaDevices>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <memory>

#include "AudioPlayer.h"
#include "jarvis/llm/LlamaCppBackend.h"
#include "jarvis/llm/ModelRegistry.h"
#include "jarvis/voice/PiperTtsBackend.h"
#include "jarvis/voice/VoiceActivityDetector.h"
#include "jarvis/voice/WhisperSttBackend.h"

using namespace jarvis;
using namespace jarvis::voice;
namespace fs = std::filesystem;

/// The whole pipeline, end to end, with every engine real.
///
///     Piper -> PCM -> VAD -> Whisper -> Qwen3 -> Piper -> AudioPlayer
///
/// Nothing here is stubbed. The only substitution is the source of the audio:
/// Piper speaks the prompt instead of a person, because a test process cannot
/// make a sound in a room. That substitution is stated in the report and is not
/// claimed to cover a live microphone.
///
/// Every stage is timed, and the numbers in the Phase 4 report come from here.
class TestVoiceIntegration : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void fullPipelineInRussian();
    void bargeInStopsPlayback();

private:
    static fs::path whisperModel();
    static fs::path piperExecutable();
    static fs::path voicePath(const char* prefix);

    void report(const QString& line);

    std::unique_ptr<WhisperSttBackend> m_stt;
    std::unique_ptr<PiperTtsBackend> m_tts;
    std::unique_ptr<llm::LlamaCppBackend> m_llm;
    bool m_ready{false};
    QStringList m_report;
};

fs::path TestVoiceIntegration::whisperModel() {
    for (const char* candidate : {"../../../models/whisper/ggml-large-v3-turbo.bin",
                                  "../../models/whisper/ggml-large-v3-turbo.bin"}) {
        const fs::path path = fs::absolute(candidate);
        if (fs::is_regular_file(path)) {
            return path;
        }
    }
    return {};
}

fs::path TestVoiceIntegration::piperExecutable() {
    const fs::path path = "C:/Users/hrant/.helolo/voice/piper/piper.exe";
    return fs::is_regular_file(path) ? path : fs::path{};
}

fs::path TestVoiceIntegration::voicePath(const char* prefix) {
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

void TestVoiceIntegration::report(const QString& line) {
    m_report.append(line);
    qInfo("%s", qPrintable(line));
}

void TestVoiceIntegration::initTestCase() {
    if (whisperModel().empty()) {
        QSKIP("Whisper model missing - the pipeline cannot be tested");
    }
    if (piperExecutable().empty() || voicePath("ru_").empty()) {
        QSKIP("Piper or the Russian voice missing - the pipeline cannot be tested");
    }

    // --- Whisper ---
    // JARVIS_STT_CPU=1 runs Whisper on the CPU. Both residency strategies have
    // to be measurable, because on an 8 GB card they are not equivalent - see
    // docs/VOICE.md.
    const bool sttOnCpu = qEnvironmentVariable("JARVIS_STT_CPU") == QStringLiteral("1");

    WhisperSttBackend::Options sttOptions;
    sttOptions.modelPath = whisperModel();
    sttOptions.useGpu = !sttOnCpu;
    m_stt = std::make_unique<WhisperSttBackend>(sttOptions);

    report(QStringLiteral("Whisper placement        : %1")
               .arg(sttOnCpu ? QStringLiteral("CPU") : QStringLiteral("GPU (Vulkan)")));

    QElapsedTimer timer;
    timer.start();
    const auto sttLoaded = m_stt->load();
    QVERIFY2(sttLoaded.has_value(),
             sttLoaded ? "" : sttLoaded.error().toUserString().c_str());
    report(QStringLiteral("Whisper load (cold)      : %1 ms").arg(timer.elapsed()));

    // --- Piper ---
    PiperTtsBackend::Options ttsOptions;
    ttsOptions.executable = piperExecutable();
    ttsOptions.voices[i18n::Language::Russian] = voicePath("ru_");
    if (const fs::path en = voicePath("en_"); !en.empty()) {
        ttsOptions.voices[i18n::Language::English] = en;
    }
    m_tts = std::make_unique<PiperTtsBackend>(ttsOptions);
    QVERIFY(m_tts->supports(i18n::Language::Russian));

    // --- Qwen3 ---
    llm::ModelRegistry::Options registryOptions;
    registryOptions.includeOllamaBlobs = true;
    llm::ModelRegistry registry{registryOptions};

    std::vector<llm::ModelInfo> models = registry.scan();
    if (models.empty()) {
        QSKIP("no GGUF language model - the pipeline cannot be tested");
    }
    std::ranges::sort(models, [](const llm::ModelInfo& a, const llm::ModelInfo& b) {
        return a.fileSizeBytes > b.fileSizeBytes;
    });

    llm::LoadParams loadParams;
    loadParams.gpuLayers = -1;
    loadParams.contextLength = 4096;

    m_llm = std::make_unique<llm::LlamaCppBackend>();

    timer.restart();
    const auto llmLoaded = m_llm->load(models.front(), loadParams);
    QVERIFY2(llmLoaded.has_value(),
             llmLoaded ? "" : llmLoaded.error().toUserString().c_str());
    report(QStringLiteral("Qwen3 load (cold)        : %1 ms").arg(timer.elapsed()));

    const llm::LoadedModel& loaded = m_llm->loadedModel();
    report(QStringLiteral("model                    : %1")
               .arg(QString::fromStdString(loaded.info.displayName())));
    report(QStringLiteral("GPU layers               : %1/%2 on %3")
               .arg(loaded.offloadedLayers)
               .arg(loaded.totalLayers)
               .arg(QString::fromStdString(loaded.deviceName)));

    // Both models are resident at once. If this is a problem it will show up as
    // a load failure above, not as a silent fallback.
    QVERIFY2(loaded.offloadedLayers > 0,
             "Whisper and Qwen3 together left no room on the GPU for the model");

    m_ready = true;
}

void TestVoiceIntegration::cleanupTestCase() {
    QFile file{QStringLiteral("voice-pipeline-report.txt")};
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_report.join(QChar{u'\n'}).toUtf8());
        file.write("\n");
    }

    if (m_llm) {
        m_llm->unload();
    }
    if (m_stt) {
        m_stt->unload();
    }
}

void TestVoiceIntegration::fullPipelineInRussian() {
    if (!m_ready) {
        QSKIP("pipeline not initialised");
    }

    QElapsedTimer total;
    total.start();

    // --- Stage 0: the "spoken" prompt -----------------------------------
    // Piper stands in for a human voice. Real audio, synthetic speaker.
    TtsRequest prompt;
    prompt.text = "Привет, Джарвис. Представься одним предложением.";
    prompt.language = i18n::Language::Russian;

    QElapsedTimer timer;
    timer.start();
    const auto spoken = m_tts->synthesize(prompt);
    QVERIFY2(spoken.has_value(), spoken ? "" : spoken.error().toUserString().c_str());
    report(QStringLiteral("prompt synthesis         : %1 ms for %2 s of audio")
               .arg(timer.elapsed())
               .arg(spoken->durationSeconds(), 0, 'f', 2));

    // --- Stage 1: PCM into the pipeline format ---------------------------
    AudioBuffer utterance;
    utterance.reserve(spoken->samples.size());
    const double ratio = static_cast<double>(spoken->sampleRate) / kSttSampleRate;
    for (std::size_t i = 0;; ++i) {
        const auto source = static_cast<std::size_t>(static_cast<double>(i) * ratio);
        if (source >= spoken->samples.size()) {
            break;
        }
        utterance.push_back(static_cast<float>(spoken->samples[source]) / 32768.0F);
    }
    QVERIFY(!utterance.empty());

    // --- Stage 2: VAD ----------------------------------------------------
    // The detector must find speech in this audio; if it does not, the live
    // pipeline would never trigger either.
    VoiceActivityDetector vad;
    timer.restart();

    bool speechDetected = false;
    const std::size_t block = kSttSampleRate / 10;  // 100 ms
    for (std::size_t offset = 0; offset < utterance.size(); offset += block) {
        const std::size_t count = std::min(block, utterance.size() - offset);
        const auto event = vad.process(utterance.data() + offset, count);
        if (event == VoiceActivityDetector::Event::SpeechStarted) {
            speechDetected = true;
            report(QStringLiteral("VAD speech start         : %1 ms into the audio")
                       .arg(offset * 1000 / kSttSampleRate));
        }
    }
    QVERIFY2(speechDetected, "the VAD found no speech in real synthesised speech");
    report(QStringLiteral("VAD scan                 : %1 ms").arg(timer.elapsed()));

    // --- Stage 3: Whisper ------------------------------------------------
    m_stt->setLanguage(i18n::Language::Russian);
    timer.restart();
    const auto transcript = m_stt->transcribe(utterance);
    QVERIFY2(transcript.has_value(),
             transcript ? "" : transcript.error().toUserString().c_str());

    report(QStringLiteral("STT latency              : %1 ms (x%2 realtime)")
               .arg(timer.elapsed())
               .arg(transcript->realtimeFactor(), 0, 'f', 1));

    QVERIFY2(!transcript->isEmpty(), "Whisper produced no text from real speech");
    m_report.append(QStringLiteral("transcript               : %1")
                        .arg(QString::fromStdString(transcript->text)));

    // --- Stage 4: Qwen3 --------------------------------------------------
    llm::GenerationRequest request;
    request.messages.push_back(
        {llm::ChatMessage::Role::System,
         "Ты — JARVIS, локальный ассистент. Отвечай по-русски, одним коротким "
         "предложением."});
    request.messages.push_back(
        {llm::ChatMessage::Role::User, transcript->text + " /no_think"});
    request.sampling.temperature = 0.0F;
    request.sampling.maxTokens = 96;

    QElapsedTimer llmTimer;
    llmTimer.start();
    qint64 firstTokenMs = -1;
    std::string answer;

    const auto stats = m_llm->generate(request, [&](std::string_view piece) {
        if (firstTokenMs < 0) {
            firstTokenMs = llmTimer.elapsed();
        }
        answer.append(piece);
        return true;
    });
    QVERIFY2(stats.has_value(), stats ? "" : stats.error().toUserString().c_str());

    report(QStringLiteral("LLM time to first token  : %1 ms").arg(firstTokenMs));
    report(QStringLiteral("LLM generation           : %1 tokens at %2 tok/s")
               .arg(stats->generatedTokens)
               .arg(stats->generatedTokensPerSecond(), 0, 'f', 1));

    // Strip the think block Qwen3 emits before its answer.
    std::string reply = answer;
    if (const std::size_t close = reply.find("</think>"); close != std::string::npos) {
        reply = reply.substr(close + 8);
    }
    while (!reply.empty() && (reply.front() == '\n' || reply.front() == ' ')) {
        reply.erase(reply.begin());
    }

    QVERIFY2(!reply.empty(), "the model produced no answer");
    m_report.append(QStringLiteral("answer                   : %1")
                        .arg(QString::fromStdString(reply)));

    // --- Stage 5: Piper speaks the answer --------------------------------
    TtsRequest speech;
    speech.text = reply;
    speech.language = i18n::Language::Russian;

    timer.restart();
    const auto audio = m_tts->synthesize(speech);
    QVERIFY2(audio.has_value(), audio ? "" : audio.error().toUserString().c_str());
    report(QStringLiteral("TTS latency              : %1 ms for %2 s of audio")
               .arg(timer.elapsed())
               .arg(audio->durationSeconds(), 0, 'f', 2));

    QVERIFY(!audio->isEmpty());

    // --- Stage 6: playback ------------------------------------------------
    if (QMediaDevices::audioOutputs().isEmpty()) {
        report(QStringLiteral("playback                 : SKIPPED, no output device"));
        QSKIP("no audio output device - playback stage cannot run");
    }

    app::AudioPlayer player;
    player.setVolume(0.4);

    QSignalSpy finished{&player, &app::AudioPlayer::playbackFinished};

    timer.restart();
    QVERIFY2(player.play(*audio), qPrintable(player.lastError()));
    report(QStringLiteral("playback start           : %1 ms").arg(timer.elapsed()));

    // The level must become a real, non-zero measurement while audio flows.
    bool sawLevel = false;
    for (int i = 0; i < 60 && !sawLevel; ++i) {
        if (player.outputLevel() > 0.001) {
            sawLevel = true;
        }
        QTest::qWait(25);
    }
    QVERIFY2(sawLevel, "the output level stayed at zero while playing the answer");

    const int timeout = static_cast<int>(audio->durationSeconds() * 1000) + 8000;
    QVERIFY2(finished.wait(timeout), "playback did not finish");
    QVERIFY(!player.isPlaying());
    QCOMPARE(player.outputLevel(), 0.0);

    report(QStringLiteral("round trip (STT+LLM+TTS) : %1 ms").arg(total.elapsed()));
}

void TestVoiceIntegration::bargeInStopsPlayback() {
    if (!m_ready) {
        QSKIP("pipeline not initialised");
    }
    if (QMediaDevices::audioOutputs().isEmpty()) {
        QSKIP("no audio output device");
    }

    // Barge-in at the level the mechanism actually works: playback is replaced
    // the moment the controller decides speech started. Here that decision is
    // made directly, because a test cannot speak into the microphone.
    TtsRequest longUtterance;
    longUtterance.text =
        "Это довольно длинное предложение, которое должно звучать несколько "
        "секунд, чтобы его можно было прервать.";
    longUtterance.language = i18n::Language::Russian;

    const auto audio = m_tts->synthesize(longUtterance);
    QVERIFY(audio.has_value());
    QVERIFY(audio->durationSeconds() > 2.0);

    app::AudioPlayer player;
    player.setVolume(0.3);
    QVERIFY(player.play(*audio));
    QVERIFY(player.isPlaying());

    QTest::qWait(400);
    QVERIFY2(player.isPlaying(), "playback ended before it could be interrupted");

    // This is what VoiceController does on a confirmed SpeechStarted.
    player.stop();

    QVERIFY2(!player.isPlaying(), "playback continued after being interrupted");
    QCOMPARE(player.outputLevel(), 0.0);

    report(QStringLiteral("barge-in                 : playback stopped mid-utterance"));
}

QTEST_MAIN(TestVoiceIntegration)

#include "tst_voice_integration.moc"
