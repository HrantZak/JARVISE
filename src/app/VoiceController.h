#pragma once

#include <QObject>
#include "SpokenCommands.h"
#include <QString>

#include <atomic>
#include <deque>
#include <memory>

#include "jarvis/core/ThreadPool.h"
#include "jarvis/voice/ISttBackend.h"
#include "jarvis/voice/ITtsBackend.h"
#include "jarvis/voice/VoiceActivityDetector.h"
#include "jarvis/voice/VoiceTypes.h"

namespace jarvis::app {

class AgentLoop;

class AiCoreModel;
class AudioCapture;
class AudioPlayer;
class LlmController;

/// Orchestrates the voice pipeline.
///
///     AudioCapture -> VAD -> ISttBackend -> LlmController -> ITtsBackend -> AudioPlayer
///
/// Nothing in that chain knows about its neighbours; this class owns every
/// connection between them. It holds no inference code of its own, and it is
/// the only place that decides what happens next.
///
/// **Threading.** Audio blocks arrive on the GUI thread and are pushed straight
/// through the VAD, which is an RMS over 100 ms - microseconds of work. The two
/// expensive stages, transcription and synthesis, run on the shared ThreadPool
/// and return through queued invocations. Generation already runs on the pool
/// inside LlmController. Nothing here blocks a frame.
///
/// **One request at a time.** The state machine refuses to start a new stage
/// while one is running, so a slow transcription cannot be overtaken by the
/// next utterance.
class VoiceController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString stateKey READ stateKey NOTIFY stateChanged)
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool listening READ isListening NOTIFY stateChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(QString lastTranscript READ lastTranscript NOTIFY transcriptChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged)
    Q_PROPERTY(QString sttModel READ sttModel NOTIFY availabilityChanged)
    Q_PROPERTY(bool sttReady READ sttReady NOTIFY availabilityChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY availabilityChanged)
    Q_PROPERTY(bool sttOnGpu READ sttOnGpu NOTIFY availabilityChanged)
    Q_PROPERTY(QString ttsVoice READ ttsVoice NOTIFY availabilityChanged)
    Q_PROPERTY(bool ttsReady READ ttsReady NOTIFY availabilityChanged)
    Q_PROPERTY(qreal vadThreshold READ vadThreshold WRITE setVadThreshold
                   NOTIFY vadChanged)

public:
    void setWakeWordRequired(bool enabled) { m_wakeWordRequired = enabled; m_wakeGate.reset(); }
    struct Dependencies {
        core::ThreadPool* pool{nullptr};
        AudioCapture* capture{nullptr};
        AudioPlayer* player{nullptr};
        voice::ISttBackend* stt{nullptr};
        voice::ITtsBackend* tts{nullptr};
        LlmController* llm{nullptr};

        /// The agent. When present, a spoken request goes through it exactly as
        /// a typed one does - same planner, same validator, same permissions,
        /// same confirmation. Speaking is not a privileged way in.
        ///
        /// Optional so the pipeline can still be tested against the model
        /// alone, but the application always supplies it.
        AgentLoop* agent{nullptr};

        AiCoreModel* core{nullptr};
    };

    explicit VoiceController(Dependencies dependencies, QObject* parent = nullptr);
    ~VoiceController() override;

    [[nodiscard]] voice::VoicePipelineState state() const noexcept { return m_state; }
    [[nodiscard]] QString stateKey() const;
    [[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }
    [[nodiscard]] bool isListening() const noexcept;

    /// False when a required piece is missing - no microphone, no Whisper
    /// model, no voice. unavailableReason() says which.
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QString unavailableReason() const { return m_unavailableReason; }

    [[nodiscard]] QString lastTranscript() const { return m_lastTranscript; }
    [[nodiscard]] QString lastError() const { return m_lastError; }

    /// Identity of the loaded recognition model, empty when none is loaded.
    [[nodiscard]] QString sttModel() const;
    [[nodiscard]] bool sttReady() const;
    bool loading() const noexcept { return m_recognitionLoading; }
    [[nodiscard]] bool sttOnGpu() const;

    /// Voice used for the current language, empty when none is installed.
    [[nodiscard]] QString ttsVoice() const;
    [[nodiscard]] bool ttsReady() const;

    /// VAD activation threshold, 0..1. The release threshold follows at half.
    [[nodiscard]] qreal vadThreshold() const;
    void setVadThreshold(qreal threshold);

    /// Turns the pipeline on or off from the interface.
    Q_INVOKABLE void setEnabled(bool enabled);
    void playGreeting();

    void setVadConfig(const voice::VoiceActivityDetector::Config& config);
    [[nodiscard]] const voice::VoiceActivityDetector::Config& vadConfig() const;

    /// Language for recognition and synthesis. Follows the interface language.
    void setLanguage(i18n::Language language);

    /// Opens the microphone and begins listening. Returns false and records an
    /// error when the pipeline cannot run.
    Q_INVOKABLE bool startListening();

    /// Closes the microphone and returns to Idle. Any playback stops.
    Q_INVOKABLE void stopListening();

    /// Aborts whatever is in flight - transcription, generation or speech - and
    /// returns to a safe state without closing the microphone.
    Q_INVOKABLE void cancel();

    /// Feeds one utterance directly, bypassing the microphone. This is how the
    /// integration test drives the pipeline with Piper-synthesised audio.
    void submitUtterance(const voice::AudioBuffer& audio);

Q_SIGNALS:
    void stateChanged();
    void enabledChanged();
    void availabilityChanged();
    void transcriptChanged();
    void lastErrorChanged();
    void vadChanged();

    /// A complete utterance was recognised. Empty transcripts never reach this.
    void transcribed(const QString& text);

    /// The pipeline finished a full turn and is back at Idle.
    void turnFinished();

    /// Speech was detected while JARVIS was talking, and playback was stopped.
    void bargedIn();

    /// The interface asked for voice while the recognition model was not
    /// loaded. The composition root owns the model's lifetime, so it does the
    /// loading; this controller only asks.
    void sttLoadRequested();

private:
    bool m_wakeWordRequired{false};
    WakeGate m_wakeGate;
    bool m_recognitionLoading{false};
    std::uint64_t m_speechGeneration{0};
    void setState(voice::VoicePipelineState state);
    void setError(const QString& message, voice::VoicePipelineState next);
    void syncCore();

    void onAudioBlock(const voice::AudioBuffer& samples);
    void onUtteranceReady(voice::AudioBuffer audio);
    void onTranscript(const voice::SttResult& result);

    void onAgentFinished(const QString& answer, bool ok);
    void onAssistantChunk(const QString& text);
    void onGenerationCompleted(const QString& answer, bool ok);

    /// Splits streamed text on sentence boundaries so synthesis can start
    /// before the model has finished writing.
    void enqueueSpeakableText(const QString& text, bool flushRemainder);
    void speakNext();

    /// Stops playback and clears the queue. Used by cancel() and barge-in.
    void abortSpeech();

    Dependencies m_deps;
    voice::VoiceActivityDetector m_vad;

    voice::VoicePipelineState m_state{voice::VoicePipelineState::Idle};
    i18n::Language m_language{i18n::kDefaultLanguage};

    QString m_lastTranscript;
    QString m_lastError;
    QString m_unavailableReason;

    /// Text waiting to be spoken, already split into sentences.
    std::deque<QString> m_speechQueue;

    /// Streamed text not yet ending in a sentence boundary.
    QString m_pendingText;

    bool m_enabled{false};

    /// Set while a stage owns the pipeline, so a second one cannot start.
    bool m_stageBusy{false};

    /// Generation and synthesis overlap while text streams. A synthesis worker
    /// owns this flag until its callback returns, even after cancellation.
    bool m_generationActive{false};
    bool m_synthesisBusy{false};

    /// Set while this controller is the one calling AgentLoop::stop().
    ///
    /// stop() ends the task synchronously and reports it as a failed turn,
    /// which is right for the agent - the task did not finish. It is wrong for
    /// the person who pressed Stop or spoke over the answer: that is not an
    /// error, and reporting it as one puts "Stopped." in the error banner and
    /// flashes the Core red after every barge-in.
    bool m_stoppingSelf{false};

    /// Guards against a queued callback landing after destruction begins.
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

} // namespace jarvis::app
