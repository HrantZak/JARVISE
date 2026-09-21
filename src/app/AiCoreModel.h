#pragma once

#include <QObject>
#include <QString>

#include <array>
#include <optional>

#include "jarvis/agent/AgentState.h"

namespace jarvis::app {

/// The AI Core's state, and the signals that drive its visuals.
///
/// This is the seam between the interface and the engines that do not exist
/// yet. Phase 3 onwards calls the C++ setters - setState(), setInputLevel(),
/// setOutputLevel(), setProgress() - and the UI reacts without a single QML
/// change. Nothing in this class fabricates a value: with no engine attached
/// the state is Offline and every level is zero.
///
/// Preview mode is the one exception, and it is explicit: previewState() forces
/// a state so the visual language can be inspected during development. It sets
/// `previewing`, which the UI shows as a badge, and it never pretends an engine
/// is running.
class AiCoreModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    /// Stable, never-translated identifier: "OFFLINE", "THINKING", ...
    /// The UI branches and the theme palette key off this.
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY stateChanged)
    /// The same state, translated for display.
    Q_PROPERTY(QString stateLabel READ stateLabel NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString detailText READ detailText NOTIFY detailTextChanged)

    Q_PROPERTY(qreal inputLevel READ inputLevel NOTIFY inputLevelChanged)
    Q_PROPERTY(qreal outputLevel READ outputLevel NOTIFY outputLevelChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

    Q_PROPERTY(bool previewing READ previewing NOTIFY previewingChanged)

public:
    enum class State {
        Offline = 0,
        Idle,
        Listening,
        Thinking,
        Planning,
        Executing,
        Confirming,
        Recovering,
        Speaking,
        Warning,
        Error,
    };
    Q_ENUM(State)

    /// Which subsystem a report came from.
    ///
    /// Each one reports only its own activity. None of them decides what the
    /// interface shows: before this existed, the voice pipeline and the tool
    /// coordinator both wrote the visual state directly and the last writer
    /// won, so a tool running inside a voice turn flickered between EXECUTING
    /// and SPEAKING depending on which happened to fire last.
    enum class Source {
        Llm = 0,   ///< Whether a model is loaded, and whether it is generating.
        Voice,     ///< Microphone, recognition, synthesis, playback.
        Tools,     ///< The tool pipeline.
        Agent,     ///< Task orchestration.
    };
    Q_ENUM(Source)

    explicit AiCoreModel(QObject* parent = nullptr);

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] QString stateKey() const;
    [[nodiscard]] QString stateLabel() const;
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] QString detailText() const { return m_detailText; }

    /// Microphone envelope, 0..1. Driven by the voice engine from Phase 4;
    /// stays at 0 until then.
    [[nodiscard]] qreal inputLevel() const noexcept { return m_inputLevel; }

    /// Speech synthesis envelope, 0..1. Driven by TTS from Phase 5.
    [[nodiscard]] qreal outputLevel() const noexcept { return m_outputLevel; }

    /// Task progress, 0..1, meaningful while Executing.
    [[nodiscard]] qreal progress() const noexcept { return m_progress; }

    [[nodiscard]] bool previewing() const noexcept { return m_previewing; }

    // --- C++ API ----------------------------------------------------------

    /// Reports one subsystem's current activity.
    ///
    /// \p state is what that subsystem is doing, not what the interface should
    /// show. Pass `std::nullopt` for "nothing to report" - a disabled voice
    /// pipeline says nothing rather than claiming the whole assistant is
    /// offline. The visible state is resolved from every source together, by
    /// urgency, in resolve().
    void report(Source source, std::optional<State> state);

    /// What one subsystem last reported. For tests and for diagnosing which
    /// source is holding the visible state.
    [[nodiscard]] std::optional<State> reported(Source source) const;

    /// The single mapping from the agent's state machine to the visual
    /// vocabulary. Nothing else may translate between the two.
    [[nodiscard]] static State coreStateFor(agent::AgentState state) noexcept;

    /// How urgent a state is. Higher wins when sources disagree; exposed so the
    /// tests can assert the ordering rather than infer it.
    [[nodiscard]] static int urgency(State state) noexcept;

    void setStatusText(const QString& text);
    void setDetailText(const QString& text);
    void setInputLevel(qreal level);
    void setOutputLevel(qreal level);
    void setProgress(qreal progress);

    // --- Development preview ---------------------------------------------

    /// Forces the visual state without claiming an engine is running.
    Q_INVOKABLE void previewState(State state);

    /// Same, by untranslated key ("EXECUTING", "CONFIRMING", ...).
    ///
    /// QML uses this rather than the enum's numeric value: the numbers shift
    /// whenever a state is inserted, and a settings page silently previewing
    /// the wrong state is the kind of bug nobody reports.
    Q_INVOKABLE void previewStateKey(const QString& key);

    /// Parses an untranslated key. Returns nullopt for anything unrecognised.
    [[nodiscard]] static std::optional<State> stateFromKey(const QString& key);

    /// Returns to the state the engines actually report.
    Q_INVOKABLE void clearPreview();

    /// Untranslated identifier for any state.
    [[nodiscard]] static QString keyForState(State state);

    /// Translated display label for any state.
    [[nodiscard]] static QString labelForState(State state);

    /// Rebuilds every tr()-derived string. Called when the language changes.
    void retranslate();

Q_SIGNALS:
    void stateChanged();
    void statusTextChanged();
    void detailTextChanged();
    void inputLevelChanged();
    void outputLevelChanged();
    void progressChanged();
    void previewingChanged();

private:
    void refreshDefaultText();

    /// Recomputes the visible state from every source. Called on every report.
    void resolve();

    static constexpr std::size_t kSourceCount = 4;
    std::array<std::optional<State>, kSourceCount> m_sources{};

    State m_engineState{State::Offline};
    State m_previewStateValue{State::Offline};
    bool m_previewing{false};

    QString m_statusText;
    QString m_detailText;

    qreal m_inputLevel{0.0};
    qreal m_outputLevel{0.0};
    qreal m_progress{0.0};
};

} // namespace jarvis::app
