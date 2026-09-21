#include "AiCoreModel.h"

#include <algorithm>

#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "core";

qreal clamped01(qreal value) {
    return std::clamp(value, qreal{0.0}, qreal{1.0});
}

} // namespace

AiCoreModel::AiCoreModel(QObject* parent)
    : QObject{parent} {
    refreshDefaultText();
}

AiCoreModel::State AiCoreModel::state() const noexcept {
    return m_previewing ? m_previewStateValue : m_engineState;
}

QString AiCoreModel::stateKey() const {
    return keyForState(state());
}

QString AiCoreModel::stateLabel() const {
    return labelForState(state());
}

QString AiCoreModel::keyForState(State state) {
    // Never translated: the QML state machine, the theme palette and the log
    // all key off these strings.
    switch (state) {
    case State::Offline:   return QStringLiteral("OFFLINE");
    case State::Idle:      return QStringLiteral("IDLE");
    case State::Listening: return QStringLiteral("LISTENING");
    case State::Thinking:   return QStringLiteral("THINKING");
    case State::Planning:   return QStringLiteral("PLANNING");
    case State::Executing:  return QStringLiteral("EXECUTING");
    case State::Confirming: return QStringLiteral("CONFIRMING");
    case State::Recovering: return QStringLiteral("RECOVERING");
    case State::Speaking:   return QStringLiteral("SPEAKING");
    case State::Warning:    return QStringLiteral("WARNING");
    case State::Error:      return QStringLiteral("ERROR");
    }
    return QStringLiteral("OFFLINE");
}

QString AiCoreModel::labelForState(State state) {
    switch (state) {
    case State::Offline:   return tr("OFFLINE");
    case State::Idle:      return tr("IDLE");
    case State::Listening: return tr("LISTENING");
    case State::Thinking:   return tr("THINKING");
    case State::Planning:   return tr("PLANNING");
    case State::Executing:  return tr("EXECUTING");
    case State::Confirming: return tr("CONFIRMING");
    case State::Recovering: return tr("RECOVERING");
    case State::Speaking:   return tr("SPEAKING");
    case State::Warning:    return tr("WARNING");
    case State::Error:      return tr("ERROR");
    }
    return tr("OFFLINE");
}

void AiCoreModel::retranslate() {
    refreshDefaultText();
    Q_EMIT stateChanged();  // stateLabel is translated
}

void AiCoreModel::refreshDefaultText() {
    // The Command Center never shows chain-of-thought; it shows what the system
    // is doing, in words a user can act on.
    switch (state()) {
    case State::Offline:
        m_statusText = tr("AI engine not connected");
        m_detailText = tr("The local model, speech and tool engines arrive in later phases.");
        break;
    case State::Idle:
        m_statusText = tr("Ready");
        m_detailText = tr("Waiting for a command.");
        break;
    case State::Listening:
        m_statusText = tr("Listening");
        m_detailText = tr("Capturing speech.");
        break;
    case State::Thinking:
        m_statusText = tr("Understanding command");
        m_detailText = tr("Working out what to do.");
        break;
    case State::Planning:
        m_statusText = tr("Planning");
        m_detailText = tr("Working out the steps.");
        break;
    case State::Executing:
        m_statusText = tr("Executing");
        m_detailText = tr("Running the requested action.");
        break;
    case State::Confirming:
        m_statusText = tr("Waiting for you");
        m_detailText = tr("An action needs your confirmation.");
        break;
    case State::Recovering:
        m_statusText = tr("Recovering");
        m_detailText = tr("A step failed. Trying to continue.");
        break;
    case State::Speaking:
        m_statusText = tr("Responding");
        m_detailText = tr("Speaking the answer.");
        break;
    case State::Warning:
        m_statusText = tr("Attention required");
        m_detailText = tr("A subsystem needs your attention.");
        break;
    case State::Error:
        m_statusText = tr("Error");
        m_detailText = tr("Something failed. Check the log for details.");
        break;
    }

    Q_EMIT statusTextChanged();
    Q_EMIT detailTextChanged();
}

int AiCoreModel::urgency(State state) noexcept {
    // Higher wins. The ordering is a judgement about what a person needs to see
    // when two things are true at once, and it is asserted in tst_agent_state.
    switch (state) {
    case State::Error:      return 100;  // never hide a failure behind a spinner
    case State::Warning:    return 90;
    case State::Confirming: return 80;   // the assistant is waiting on the human
    case State::Recovering: return 70;
    case State::Executing:  return 60;   // something is happening to the machine
    case State::Planning:   return 50;
    case State::Listening:  return 40;   // the microphone is open: say so
    case State::Speaking:   return 30;
    case State::Thinking:   return 20;
    case State::Idle:       return 10;
    case State::Offline:    return 0;
    }
    return 0;
}

AiCoreModel::State AiCoreModel::coreStateFor(agent::AgentState state) noexcept {
    // The only translation between the agent's state machine and the visual
    // vocabulary. Several agent states share one appearance on purpose:
    // Understanding and Evaluating are both "the model is working", and a user
    // gains nothing from being told which.
    switch (state) {
    case agent::AgentState::Idle:                 return State::Idle;
    case agent::AgentState::Listening:            return State::Listening;
    case agent::AgentState::Understanding:        return State::Thinking;
    case agent::AgentState::Planning:             return State::Planning;
    case agent::AgentState::AwaitingConfirmation: return State::Confirming;
    case agent::AgentState::Executing:            return State::Executing;
    case agent::AgentState::WaitingForTool:       return State::Executing;
    case agent::AgentState::Evaluating:           return State::Thinking;
    case agent::AgentState::Recovering:           return State::Recovering;
    case agent::AgentState::Speaking:             return State::Speaking;
    case agent::AgentState::Completed:            return State::Idle;
    case agent::AgentState::Cancelled:            return State::Idle;
    case agent::AgentState::Failed:               return State::Error;
    case agent::AgentState::Unavailable:          return State::Offline;
    }
    return State::Offline;
}

void AiCoreModel::report(Source source, std::optional<State> state) {
    const auto index = static_cast<std::size_t>(source);
    if (index >= kSourceCount || m_sources[index] == state) {
        return;
    }
    m_sources[index] = state;
    resolve();
}

std::optional<AiCoreModel::State> AiCoreModel::reported(Source source) const {
    const auto index = static_cast<std::size_t>(source);
    return index < kSourceCount ? m_sources[index] : std::nullopt;
}

void AiCoreModel::resolve() {
    // The most urgent thing any subsystem is doing. With nothing reported at
    // all the assistant is offline - which is the truth at start-up, before a
    // model is loaded.
    State resolved = State::Offline;
    for (const std::optional<State>& reported : m_sources) {
        if (reported && urgency(*reported) > urgency(resolved)) {
            resolved = *reported;
        }
    }

    if (m_engineState == resolved) {
        return;
    }
    m_engineState = resolved;

    JARVIS_LOG_INFO(kCategory, "engine state -> {}",
                    keyForState(resolved).toStdString());

    if (!m_previewing) {
        refreshDefaultText();
        Q_EMIT stateChanged();
    }
}

void AiCoreModel::setStatusText(const QString& text) {
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    Q_EMIT statusTextChanged();
}

void AiCoreModel::setDetailText(const QString& text) {
    if (m_detailText == text) {
        return;
    }
    m_detailText = text;
    Q_EMIT detailTextChanged();
}

void AiCoreModel::setInputLevel(qreal level) {
    const qreal value = clamped01(level);
    if (qFuzzyCompare(m_inputLevel, value)) {
        return;
    }
    m_inputLevel = value;
    Q_EMIT inputLevelChanged();
}

void AiCoreModel::setOutputLevel(qreal level) {
    const qreal value = clamped01(level);
    if (qFuzzyCompare(m_outputLevel, value)) {
        return;
    }
    m_outputLevel = value;
    Q_EMIT outputLevelChanged();
}

void AiCoreModel::setProgress(qreal progress) {
    const qreal value = clamped01(progress);
    if (qFuzzyCompare(m_progress, value)) {
        return;
    }
    m_progress = value;
    Q_EMIT progressChanged();
}

void AiCoreModel::previewState(State state) {
    const bool wasPreviewing = m_previewing;
    m_previewing = true;
    m_previewStateValue = state;

    JARVIS_LOG_INFO(kCategory, "visual preview -> {} (no engine is attached)",
                    keyForState(state).toStdString());

    refreshDefaultText();
    Q_EMIT stateChanged();
    if (!wasPreviewing) {
        Q_EMIT previewingChanged();
    }
}

std::optional<AiCoreModel::State> AiCoreModel::stateFromKey(const QString& key) {
    static const std::array<State, 11> kStates{
        State::Offline,   State::Idle,       State::Listening, State::Thinking,
        State::Planning,  State::Executing,  State::Confirming, State::Recovering,
        State::Speaking,  State::Warning,    State::Error,
    };

    for (const State state : kStates) {
        if (keyForState(state) == key) {
            return state;
        }
    }
    return std::nullopt;
}

void AiCoreModel::previewStateKey(const QString& key) {
    if (const std::optional<State> state = stateFromKey(key)) {
        previewState(*state);
        return;
    }
    JARVIS_LOG_WARN(kCategory, "preview requested for unknown state '{}'",
                    key.toStdString());
}

void AiCoreModel::clearPreview() {
    if (!m_previewing) {
        return;
    }
    m_previewing = false;

    JARVIS_LOG_INFO(kCategory, "visual preview cleared, back to {}",
                    keyForState(m_engineState).toStdString());

    refreshDefaultText();
    Q_EMIT stateChanged();
    Q_EMIT previewingChanged();
}

} // namespace jarvis::app
