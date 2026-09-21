#include "VoiceController.h"

#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <utility>

#include "AgentLoop.h"
#include "AiCoreModel.h"
#include "AudioCapture.h"
#include "AudioPlayer.h"
#include "LlmController.h"
#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "voice";

/// A sentence is worth speaking once it ends and is long enough to be a
/// sentence. Below this, punctuation is usually an abbreviation or a decimal
/// point, and cutting there makes Piper stutter.
constexpr int kMinimumSentenceChars = 12;

bool endsSentence(QChar ch) {
    return ch == u'.' || ch == u'!' || ch == u'?' || ch == u'…' || ch == u'\n';
}

} // namespace

VoiceController::VoiceController(Dependencies dependencies, QObject* parent)
    : QObject{parent}
    , m_deps{dependencies} {
    if (m_deps.capture != nullptr) {
        connect(m_deps.capture, &AudioCapture::audioReady, this,
                &VoiceController::onAudioBlock);
        connect(m_deps.capture, &AudioCapture::captureFailed, this,
                [this](const QString& reason) {
                    setError(reason, voice::VoicePipelineState::Error);
                });
    }

    if (m_deps.player != nullptr) {
        // When one sentence finishes, start the next. The queue is what lets
        // JARVIS begin speaking before the model has finished writing.
        connect(m_deps.player, &AudioPlayer::playbackFinished, this,
                [this] { speakNext(); });
        connect(m_deps.player, &AudioPlayer::playbackFailed, this,
                [this](const QString& reason) {
                    setError(reason, voice::VoicePipelineState::Error);
                });
    }

    if (m_deps.agent != nullptr) {
        // With the agent in charge, a turn ends when the *task* ends - not when
        // a generation does. A task can take several generations: one that asks
        // for a tool, one that phrases the answer.
        //
        // The streaming chunks are deliberately not connected here. They carry
        // whatever the model is producing at that moment, which for a tool
        // round is a JSON object: connecting them would have JARVIS read a tool
        // call aloud and then fall silent before the real answer. Speaking the
        // finished answer costs the incremental synthesis Phase 4 built, and
        // that is the right trade - see docs/phase6-architecture.md.
        connect(m_deps.agent, &AgentLoop::finished, this,
                &VoiceController::onAgentFinished);
    } else if (m_deps.llm != nullptr) {
        // No agent: the pipeline talks to the model directly, as it did before
        // Phase 6. Kept so the voice stages can be tested without one.
        connect(m_deps.llm, &LlmController::assistantChunk, this,
                &VoiceController::onAssistantChunk);
        connect(m_deps.llm, &LlmController::generationCompleted, this,
                &VoiceController::onGenerationCompleted);
    }

    // Called for its side effect: it fills in unavailableReason for the UI.
    static_cast<void>(isAvailable());
}

VoiceController::~VoiceController() {
    // Callbacks queued onto the pool hold a weak marker; clearing it here makes
    // them no-ops rather than a use-after-free.
    *m_alive = false;

    if (m_deps.stt != nullptr) {
        m_deps.stt->requestStop();
    }
    if (m_deps.tts != nullptr) {
        m_deps.tts->requestStop();
    }
    if (m_deps.capture != nullptr) {
        m_deps.capture->stop();
    }
    if (m_deps.player != nullptr) {
        m_deps.player->stop();
    }
    if (m_deps.pool != nullptr) {
        m_deps.pool->waitIdle();
    }
}

QString VoiceController::stateKey() const {
    const std::string_view key = voice::pipelineStateKey(m_state);
    return QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
}

bool VoiceController::isListening() const noexcept {
    return m_state == voice::VoicePipelineState::Listening;
}

bool VoiceController::isAvailable() const {
    auto* self = const_cast<VoiceController*>(this);
    if (m_recognitionLoading) {
        self->m_unavailableReason = tr("Loading speech recognition...");
        return false;
    }

    if (m_deps.capture == nullptr || !m_deps.capture->isAvailable()) {
        self->m_unavailableReason = tr("No microphone is available.");
        return false;
    }
    if (m_deps.stt == nullptr || !m_deps.stt->isLoaded()) {
        self->m_unavailableReason = tr("The speech recognition model is not loaded.");
        return false;
    }
    if (m_deps.tts == nullptr || !m_deps.tts->supports(m_language)) {
        self->m_unavailableReason = tr("No voice is installed for this language.");
        return false;
    }
    if (m_deps.player == nullptr || !m_deps.player->isAvailable()) {
        self->m_unavailableReason = tr("No speaker is available.");
        return false;
    }
    if (m_deps.agent == nullptr && (m_deps.llm == nullptr || !m_deps.llm->loaded())) {
        self->m_unavailableReason = tr("No language model is loaded.");
        return false;
    }

    self->m_unavailableReason.clear();
    return true;
}

QString VoiceController::sttModel() const {
    if (m_recognitionLoading) return {};
    if (m_deps.stt == nullptr || !m_deps.stt->isLoaded()) {
        return {};
    }
    return QString::fromStdString(m_deps.stt->modelDescription());
}

bool VoiceController::sttReady() const {
    return !m_recognitionLoading && m_deps.stt != nullptr && m_deps.stt->isLoaded();
}

bool VoiceController::sttOnGpu() const {
    return !m_recognitionLoading && m_deps.stt != nullptr && m_deps.stt->isGpuAccelerated();
}

QString VoiceController::ttsVoice() const {
    if (m_deps.tts == nullptr) {
        return {};
    }
    return QString::fromStdString(m_deps.tts->voiceName(m_language));
}

bool VoiceController::ttsReady() const {
    return m_deps.tts != nullptr && m_deps.tts->supports(m_language);
}

qreal VoiceController::vadThreshold() const {
    return static_cast<qreal>(m_vad.config().activationThreshold);
}

void VoiceController::setVadThreshold(qreal threshold) {
    voice::VoiceActivityDetector::Config config = m_vad.config();
    const auto value = static_cast<float>(std::clamp(threshold, 0.001, 0.5));
    if (qFuzzyCompare(config.activationThreshold + 1.0F, value + 1.0F)) {
        return;
    }

    config.activationThreshold = value;
    // The release threshold always trails the activation one, or speech would
    // end the instant it started.
    config.releaseThreshold = value * 0.5F;
    m_vad.setConfig(config);

    JARVIS_LOG_INFO(kCategory, "VAD activation threshold -> {:.4f}", value);
    Q_EMIT vadChanged();
}

void VoiceController::playGreeting() {
    if (!m_deps.player || m_deps.player->isPlaying() || m_stageBusy || m_synthesisBusy) return;
    if (!m_deps.tts || !m_deps.tts->supports(m_language)) return;
    enqueueSpeakableText(QStringLiteral("К вашим услугам, сэр."), true);
}
void VoiceController::setEnabled(bool enabled) {
    if (!enabled) {
        stopListening();
        return;
    }

    if (m_recognitionLoading) {
        m_enabled = true;
        Q_EMIT enabledChanged();
        return;
    }
    if (m_deps.stt != nullptr && !m_deps.stt->isLoaded()) {
        if (!m_deps.pool) return;
        m_recognitionLoading = true;
        m_enabled = true;
        Q_EMIT availabilityChanged();
        Q_EMIT enabledChanged();
        m_deps.pool->post([this] {
            const auto result = m_deps.stt->load();
            QMetaObject::invokeMethod(this, [this, result] {
                m_recognitionLoading = false;
                Q_EMIT availabilityChanged();
                if (!result) {
                    m_enabled = false;
                    Q_EMIT enabledChanged();
                    setError(QString::fromStdString(result.error().toUserString()), voice::VoicePipelineState::Unavailable);
                } else if (m_enabled) {
                    static_cast<void>(startListening());
                }
            }, Qt::QueuedConnection);
        });
        return;
    }

    static_cast<void>(startListening());
}

void VoiceController::setVadConfig(const voice::VoiceActivityDetector::Config& config) {
    m_vad.setConfig(config);
    Q_EMIT vadChanged();
}

const voice::VoiceActivityDetector::Config& VoiceController::vadConfig() const {
    return m_vad.config();
}

void VoiceController::setLanguage(i18n::Language language) {
    if (m_language == language) {
        return;
    }
    m_language = language;

    if (m_deps.stt != nullptr) {
        m_deps.stt->setLanguage(language);
    }

    JARVIS_LOG_INFO(kCategory, "voice language -> {}", i18n::languageCode(language));
    Q_EMIT availabilityChanged();
}

void VoiceController::setState(voice::VoicePipelineState state) {
    if (m_state == state) {
        return;
    }

    JARVIS_LOG_INFO(kCategory, "{} -> {}", voice::pipelineStateKey(m_state),
                    voice::pipelineStateKey(state));

    m_state = state;
    syncCore();
    Q_EMIT stateChanged();
}

void VoiceController::syncCore() {
    if (m_deps.core == nullptr) {
        return;
    }

    // Reports what the pipeline is doing. The Core decides what to show, from
    // this report and every other subsystem's together.
    //
    // Two cases report nothing at all rather than Offline: a disabled or
    // unavailable voice pipeline is not a statement about the assistant as a
    // whole, and saying Offline here used to drag the whole interface offline
    // while the model was perfectly well loaded.
    switch (m_state) {
    case voice::VoicePipelineState::Listening:
        m_deps.core->report(AiCoreModel::Source::Voice, AiCoreModel::State::Listening);
        break;
    case voice::VoicePipelineState::Transcribing:
    case voice::VoicePipelineState::Thinking:
        m_deps.core->report(AiCoreModel::Source::Voice, AiCoreModel::State::Thinking);
        break;
    case voice::VoicePipelineState::Synthesizing:
    case voice::VoicePipelineState::Speaking:
        m_deps.core->report(AiCoreModel::Source::Voice, AiCoreModel::State::Speaking);
        break;
    case voice::VoicePipelineState::Error:
        m_deps.core->report(AiCoreModel::Source::Voice, AiCoreModel::State::Error);
        break;
    case voice::VoicePipelineState::Unavailable:
    case voice::VoicePipelineState::Disabled:
    case voice::VoicePipelineState::Idle:
        m_deps.core->report(AiCoreModel::Source::Voice, std::nullopt);
        break;
    }
}

void VoiceController::setError(const QString& message,
                               voice::VoicePipelineState next) {
    m_lastError = message;
    JARVIS_LOG_WARN(kCategory, "{}", message.toStdString());
    Q_EMIT lastErrorChanged();

    ++m_speechGeneration;
    if (m_synthesisBusy && m_deps.tts) m_deps.tts->requestStop();
    abortSpeech();
    m_stageBusy = false;

    setState(voice::VoicePipelineState::Error);

    // Do not sit in Error: fall back to something the user can act from.
    setState(next);
}

bool VoiceController::startListening() {
    if (!isAvailable()) {
        m_enabled = false;
        Q_EMIT enabledChanged();
        m_lastError = m_unavailableReason;
        Q_EMIT lastErrorChanged();
        setState(voice::VoicePipelineState::Unavailable);
        return false;
    }

    if (m_enabled && m_state == voice::VoicePipelineState::Listening) {
        return true;
    }

    if (!m_deps.capture->start()) {
        setError(m_deps.capture->lastError(), voice::VoicePipelineState::Unavailable);
        return false;
    }

    m_vad.reset();
    m_enabled = true;
    m_lastError.clear();

    JARVIS_LOG_INFO(kCategory, "voice pipeline started");

    Q_EMIT lastErrorChanged();
    Q_EMIT enabledChanged();
    setState(voice::VoicePipelineState::Listening);
    return true;
}

void VoiceController::stopListening() {
    m_wakeGate.reset();
    if (!m_enabled && m_state == voice::VoicePipelineState::Idle) {
        return;
    }
    ++m_speechGeneration;
    if (m_deps.stt) m_deps.stt->requestStop();
    if (m_synthesisBusy && m_deps.tts) m_deps.tts->requestStop();
    if (m_generationActive && m_deps.agent) {
        m_stoppingSelf = true;
        m_deps.agent->stop();
        m_stoppingSelf = false;
    } else if (m_generationActive && m_deps.llm) {
        m_stoppingSelf = true;
        m_deps.llm->cancel();
        m_stoppingSelf = false;
    }
    m_stageBusy = false;

    if (m_deps.capture != nullptr) {
        m_deps.capture->stop();
    }
    abortSpeech();

    m_vad.reset();
    m_enabled = false;

    JARVIS_LOG_INFO(kCategory, "voice pipeline stopped");

    Q_EMIT enabledChanged();
    setState(voice::VoicePipelineState::Idle);
}

void VoiceController::cancel() {
    ++m_speechGeneration;
    if (m_recognitionLoading) { stopListening(); return; }
    JARVIS_LOG_INFO(kCategory, "cancelling the current turn");

    if (m_deps.stt != nullptr) {
        m_deps.stt->requestStop();
    }
    if (m_deps.tts != nullptr) {
        m_deps.tts->requestStop();
    }
    if (m_deps.agent != nullptr) {
        // Global Stop. Cancelling the generation alone would leave the task
        // running - it would simply start another generation. The agent's
        // stop is the one that abandons the whole thing, and it is
        // idempotent, so calling it here and from the interface is safe.
        //
        // It also emits finished(ok = false) before returning, and this is the
        // one case where that is not an error to show the user.
        m_stoppingSelf = true;
        m_deps.agent->stop();
        m_stoppingSelf = false;
    } else if (m_deps.llm != nullptr) {
        m_stoppingSelf = true;
        m_deps.llm->cancel();
        m_stoppingSelf = false;
    }

    abortSpeech();
    m_vad.reset();
    m_stageBusy = false;

    setState(m_enabled ? voice::VoicePipelineState::Listening
                       : voice::VoicePipelineState::Idle);
}

void VoiceController::abortSpeech() {
    m_generationActive = false;
    m_speechQueue.clear();
    m_pendingText.clear();
    if (m_deps.player != nullptr) {
        m_deps.player->stop();
    }
}

void VoiceController::onAudioBlock(const voice::AudioBuffer& samples) {
    if (!m_enabled || samples.empty()) {
        return;
    }
    // Do not let the loudspeaker acknowledgement become the next command.
    // Wake-word mode accepts the user after playback has finished.
    if (m_wakeWordRequired && (m_state == voice::VoicePipelineState::Speaking ||
                              m_state == voice::VoicePipelineState::Synthesizing)) {
        m_vad.reset();
        return;
    }

    const auto event = m_vad.process(samples.data(), samples.size());

    // Barge-in. Only a confirmed speech *start* interrupts - the VAD needs the
    // activation threshold crossed, so a keyboard click or a cough does not cut
    // JARVIS off mid-sentence.
    const bool speaking = m_state == voice::VoicePipelineState::Speaking ||
                          m_state == voice::VoicePipelineState::Synthesizing;

    if (speaking && event == voice::VoiceActivityDetector::Event::SpeechStarted) {
        ++m_speechGeneration;
        JARVIS_LOG_INFO(kCategory, "barge-in: user started speaking, stopping playback");
        if (m_deps.tts != nullptr) {
            m_deps.tts->requestStop();
        }
        if (m_deps.agent != nullptr) {
            m_stoppingSelf = true;
            m_deps.agent->stop();
            m_stoppingSelf = false;
        } else if (m_deps.llm != nullptr) {
            m_stoppingSelf = true;
            m_deps.llm->cancel();
            m_stoppingSelf = false;
        }

        abortSpeech();
        m_stageBusy = false;
        setState(voice::VoicePipelineState::Listening);
        Q_EMIT bargedIn();
        return;
    }

    // While a stage owns the pipeline, keep measuring the level but do not
    // start a second utterance.
    if (m_stageBusy && m_state != voice::VoicePipelineState::Listening) {
        return;
    }

    if (event == voice::VoiceActivityDetector::Event::SpeechEnded ||
        event == voice::VoiceActivityDetector::Event::Aborted) {
        onUtteranceReady(m_vad.takeUtterance());
    }
}

void VoiceController::submitUtterance(const voice::AudioBuffer& audio) {
    onUtteranceReady(audio);
}

void VoiceController::onUtteranceReady(voice::AudioBuffer audio) {
    if (audio.empty() || m_deps.stt == nullptr || m_deps.pool == nullptr) {
        return;
    }
    if (m_stageBusy) {
        JARVIS_LOG_DEBUG(kCategory, "an utterance arrived while busy; dropping it");
        return;
    }

    m_stageBusy = true;
    setState(voice::VoicePipelineState::Transcribing);

    JARVIS_LOG_INFO(kCategory, "transcribing {:.2f}s of speech",
                    static_cast<double>(audio.size()) / voice::kSttSampleRate);

    auto alive = m_alive;
    const auto generation = ++m_speechGeneration;
    m_deps.pool->post([this, alive, generation, audio = std::move(audio)] {
        core::Result<voice::SttResult> result = m_deps.stt->transcribe(audio);

        QMetaObject::invokeMethod(
            this,
            [this, alive, generation, result = std::move(result)] {
                if (!*alive || generation != m_speechGeneration) {
                    return;
                }
                if (!result) {
                    setError(QString::fromStdString(result.error().toUserString()),
                             m_enabled ? voice::VoicePipelineState::Listening
                                       : voice::VoicePipelineState::Idle);
                    return;
                }
                onTranscript(*result);
            },
            Qt::QueuedConnection);
    });
}

void VoiceController::onTranscript(const voice::SttResult& result) {
    m_stageBusy = false;

    // Length only: the spoken words are the user's, and the log is not the
    // place for them.
    JARVIS_LOG_INFO(kCategory,
                    "transcript ready: {} characters, {:.0f} ms, x{:.1f} realtime",
                    result.text.size(), result.processingMs, result.realtimeFactor());

    if (result.isEmpty()) {
        // Silence or unintelligible speech. Sending an empty prompt to the
        // model would produce an answer to a question nobody asked.
        JARVIS_LOG_INFO(kCategory, "empty transcript; nothing is sent to the model");
        setState(m_enabled ? voice::VoicePipelineState::Listening
                           : voice::VoicePipelineState::Idle);
        return;
    }

    QString command = QString::fromStdString(result.text);
    if (m_wakeWordRequired) {
        const auto gated = m_wakeGate.consume(command, QDateTime::currentMSecsSinceEpoch());
        if (!gated.accepted) {
            setState(m_enabled ? voice::VoicePipelineState::Listening : voice::VoicePipelineState::Idle);
            if (gated.awakened) {
                m_pendingText.clear(); m_speechQueue.clear();
                enqueueSpeakableText(QStringLiteral("Слушаю, сэр."), true);
            }
            return;
        }
        command = gated.command;
    }
    m_lastTranscript = command;
    Q_EMIT transcriptChanged();
    Q_EMIT transcribed(m_lastTranscript);

    if (m_deps.agent == nullptr && (m_deps.llm == nullptr || !m_deps.llm->loaded())) {
        setError(tr("No language model is loaded."),
                 m_enabled ? voice::VoicePipelineState::Listening
                           : voice::VoicePipelineState::Idle);
        return;
    }

    m_pendingText.clear();
    m_speechQueue.clear();
    m_stageBusy = true;
    m_generationActive = true;
    setState(voice::VoicePipelineState::Thinking);

    if (m_deps.agent != nullptr) {
        // The same entry point a typed message uses, with the origin recorded.
        // Nothing about speaking grants anything: the request meets the planner,
        // the validator, the permission manager and the confirmation dialog in
        // exactly the same order.
        if (!m_deps.agent->submit(m_lastTranscript, /*fromVoice=*/true)) {
            setError(tr("The assistant is busy with another request."),
                     m_enabled ? voice::VoicePipelineState::Listening
                               : voice::VoicePipelineState::Idle);
        }
        return;
    }

    // send() only notifies the application's AgentLoop. Without an agent the
    // voice controller must start the model generation itself.
    m_deps.llm->generate(m_lastTranscript);
}

void VoiceController::onAgentFinished(const QString& answer, bool ok) {
    if (m_state != voice::VoicePipelineState::Thinking
        && m_state != voice::VoicePipelineState::Synthesizing
        && m_state != voice::VoicePipelineState::Speaking) {
        // Not this pipeline's turn: a typed request, or one that ended after a
        // barge-in already returned the microphone to listening.
        return;
    }

    if (m_stoppingSelf) {
        // This is our own stop coming back. cancel() and the barge-in path
        // clear the queue, release the stage and set the state immediately
        // after; there is nothing to report and nothing to say.
        return;
    }

    if (!ok) {
        // The task failed or was denied. The reason travels in `answer`;
        // showing it beats a silent return to Listening after the user has
        // asked a question out loud.
        setError(answer.isEmpty() ? tr("The model could not answer.") : answer,
                 m_enabled ? voice::VoicePipelineState::Listening
                           : voice::VoicePipelineState::Idle);
        return;
    }

    // The whole answer arrives at once, so there is no remainder to hold back.
    // enqueueSpeakableText() starts synthesis itself when the player is idle;
    // calling speakNext() as well would pop a second sentence and synthesise two
    // at a time.
    m_generationActive = false;
    enqueueSpeakableText(briefSpeech(answer, m_lastTranscript), /*flushRemainder=*/true);
}

void VoiceController::onAssistantChunk(const QString& text) {
    if (m_state != voice::VoicePipelineState::Thinking &&
        m_state != voice::VoicePipelineState::Synthesizing &&
        m_state != voice::VoicePipelineState::Speaking) {
        return;  // text from a typed message, not from this voice turn
    }
    enqueueSpeakableText(text, /*flushRemainder=*/false);
}

void VoiceController::onGenerationCompleted(const QString& /*answer*/, bool ok) {
    if (m_stoppingSelf) return;
    if (m_state != voice::VoicePipelineState::Thinking &&
        m_state != voice::VoicePipelineState::Synthesizing &&
        m_state != voice::VoicePipelineState::Speaking) {
        return;
    }

    if (!ok) {
        setError(tr("The model could not answer."),
                 m_enabled ? voice::VoicePipelineState::Listening
                           : voice::VoicePipelineState::Idle);
        return;
    }

    m_generationActive = false;
    // Whatever is left over is a sentence too, even without final punctuation.
    // Finishing generation must not finish the turn while synthesis is pending.
    enqueueSpeakableText({}, /*flushRemainder=*/true);
}

void VoiceController::enqueueSpeakableText(const QString& text, bool flushRemainder) {
    m_pendingText += text;

    // Cut on sentence boundaries so synthesis can start while the model is
    // still writing.
    int cut = -1;
    for (int i = 0; i < m_pendingText.size(); ++i) {
        if (endsSentence(m_pendingText.at(i)) && i + 1 >= kMinimumSentenceChars) {
            cut = i;
        }
    }

    if (cut >= 0) {
        const QString sentence = m_pendingText.left(cut + 1).trimmed();
        m_pendingText.remove(0, cut + 1);
        if (!sentence.isEmpty()) {
            m_speechQueue.push_back(sentence);
        }
    }

    if (flushRemainder) {
        const QString remainder = m_pendingText.trimmed();
        m_pendingText.clear();
        if (!remainder.isEmpty()) {
            m_speechQueue.push_back(remainder);
        }
    }

    speakNext();
}

void VoiceController::speakNext() {
    if (m_synthesisBusy || (m_deps.player && m_deps.player->isPlaying())) {
        return;
    }
    if (m_speechQueue.empty()) {
        // Nothing left to say. If generation has also finished, the turn ends.
        if (!m_generationActive && (m_state == voice::VoicePipelineState::Thinking ||
                             m_state == voice::VoicePipelineState::Speaking ||
                             m_state == voice::VoicePipelineState::Synthesizing)) {
            m_stageBusy = false;
            setState(m_enabled ? voice::VoicePipelineState::Listening
                               : voice::VoicePipelineState::Idle);
            Q_EMIT turnFinished();
        }
        return;
    }

    if (m_deps.tts == nullptr || m_deps.player == nullptr || m_deps.pool == nullptr) {
        return;
    }

    const QString sentence = m_speechQueue.front();
    m_speechQueue.pop_front();

    m_stageBusy = true;
    m_synthesisBusy = true;
    setState(voice::VoicePipelineState::Synthesizing);

    voice::TtsRequest request;
    request.text = sentence.toStdString();
    request.language = m_language;

    auto alive = m_alive;
    const auto generation = m_speechGeneration;
    m_deps.pool->post([this, alive, generation, request = std::move(request)] {
        core::Result<voice::TtsResult> audio = m_deps.tts->synthesize(request);

        QMetaObject::invokeMethod(
            this,
            [this, alive, generation, audio = std::move(audio)] {
                if (!*alive) return;
                m_synthesisBusy = false;
                if (generation != m_speechGeneration) {
                    // A new turn may already be waiting for the cancelled
                    // worker to release the backend. Never play its old audio.
                    speakNext();
                    return;
                }
                // Refresh the displayed voice after a runtime fallback or a
                // successful return to the user's reference voice.
                Q_EMIT availabilityChanged();
                if (!audio) {
                    setError(QString::fromStdString(audio.error().toUserString()),
                             m_enabled ? voice::VoicePipelineState::Listening
                                       : voice::VoicePipelineState::Idle);
                    return;
                }

                JARVIS_LOG_INFO(kCategory, "speaking {:.2f}s (synthesised in {:.0f} ms)",
                                audio->durationSeconds(), audio->synthesisMs);

                if (!m_deps.player->play(*audio)) {
                    setError(m_deps.player->lastError(),
                             m_enabled ? voice::VoicePipelineState::Listening
                                       : voice::VoicePipelineState::Idle);
                    return;
                }
                setState(voice::VoicePipelineState::Speaking);
            },
            Qt::QueuedConnection);
    });
}

} // namespace jarvis::app
