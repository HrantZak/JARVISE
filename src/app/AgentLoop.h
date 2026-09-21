#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <chrono>
#include <memory>
#include <optional>
#include <functional>

#include "jarvis/agent/AgentState.h"
#include "jarvis/agent/Memory.h"
#include "jarvis/agent/Planner.h"
#include "jarvis/agent/Recovery.h"
#include "jarvis/agent/Task.h"
#include "jarvis/config/AppConfig.h"

namespace jarvis::app {

class AiCoreModel;
class LlmController;
class ToolCoordinator;

/// Runs one task, from a request to an answer.
///
/// **The only orchestrator.** `LlmController` generates text and owns the model;
/// `ToolCoordinator` validates, asks and executes; this class decides what
/// happens next. Until Phase 6 the loop lived inside `LlmController`, which
/// meant the component that talks to the model also decided when to run tools -
/// two jobs that pull apart as soon as a task has more than one step.
///
/// It has no privileges of its own. It cannot reach `ToolExecutor`, evaluate a
/// permission, mint a confirmation or construct a `ValidatedCall`. Every action
/// goes to the coordinator, which applies the same boundary it applies to a
/// single conversational call.
class AgentLoop : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY taskChanged)
    Q_PROPERTY(QString agentState READ agentStateKey NOTIFY taskChanged)
    Q_PROPERTY(QString agentStateLabel READ agentStateLabel NOTIFY taskChanged)
    Q_PROPERTY(QString taskId READ taskId NOTIFY taskChanged)
    Q_PROPERTY(QString taskStatus READ taskStatusKey NOTIFY taskChanged)
    Q_PROPERTY(QString userRequest READ userRequest NOTIFY taskChanged)
    Q_PROPERTY(int currentStep READ currentStep NOTIFY taskChanged)
    Q_PROPERTY(int totalSteps READ totalSteps NOTIFY taskChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY taskChanged)
    Q_PROPERTY(QString currentTool READ currentTool NOTIFY taskChanged)
    Q_PROPERTY(int retryCount READ retryCount NOTIFY taskChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY taskChanged)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY taskChanged)
    Q_PROPERTY(bool plannerEnabled READ plannerEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool memoryEnabled READ memoryEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool memoryPersistent READ memoryPersistent NOTIFY settingsChanged)
    Q_PROPERTY(int memoryEntryCount READ memoryEntryCount NOTIFY taskChanged)

public:
    AgentLoop(LlmController& llm, ToolCoordinator& tools, AiCoreModel& core,
              QObject* parent = nullptr);
    ~AgentLoop() override;

    void applySettings(const config::AgentSettings& settings);

    [[nodiscard]] bool enabled() const noexcept { return m_settings.enabled; }
    [[nodiscard]] bool plannerEnabled() const noexcept {
        return m_settings.plannerEnabled;
    }
    [[nodiscard]] bool busy() const noexcept { return m_task != nullptr || m_localPending; }
    [[nodiscard]] QString agentStateKey() const;
    [[nodiscard]] QString agentStateLabel() const;
    [[nodiscard]] QString taskId() const;
    [[nodiscard]] QString taskStatusKey() const;
    [[nodiscard]] QString userRequest() const;
    [[nodiscard]] int currentStep() const;
    [[nodiscard]] int totalSteps() const;
    [[nodiscard]] qreal progress() const;
    [[nodiscard]] QString currentTool() const { return m_currentTool; }
    [[nodiscard]] int retryCount() const { return m_retries; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QVariantList steps() const;

    [[nodiscard]] bool memoryEnabled() const noexcept {
        return m_memory.config().enabled;
    }
    [[nodiscard]] bool memoryPersistent() const noexcept {
        return m_memory.config().persistent;
    }
    [[nodiscard]] int memoryEntryCount() const {
        return static_cast<int>(m_memory.size());
    }

    /// The catalogue and plan format, for the system prompt.
    [[nodiscard]] QString promptSection() const;

    /// What the agent remembers between tasks. Owned here because the loop is
    /// what decides when something is worth recording; the store decides
    /// whether it is allowed to be.
    [[nodiscard]] agent::MemoryStore& memory() noexcept { return m_memory; }

    /// Starts a task. Refused while one is running: the agent does one thing at
    /// a time, so a confirmation is never ambiguous about what it belongs to.
    Q_INVOKABLE bool submit(const QString& request, bool fromVoice = false);

    /// Stops everything that can be stopped - generation, the running tool, a
    /// pending confirmation, the task. Idempotent and safe at any time.
    Q_INVOKABLE void stop();

    void retranslate();
    std::function<std::optional<QString>(const QString&)> localCommand;

Q_SIGNALS:
    void settingsChanged();
    void taskChanged();

    /// The exchange ended. \p ok is false when the task failed or was stopped.
    void finished(const QString& answer, bool ok);

private:
    bool m_localPending{false};
    bool m_screenArmed{false};
    bool m_directLaunch{false};
    bool m_directShell{false};
    QString m_expectedArtifact;
    int m_artifactRepairs{0};
    void onGenerationCompleted(const QString& answer, bool ok);
    void onToolFinished(bool hadToolCall, const QString& resultText,
                        const QString& toolName);

    void interpret(const QString& answer);
    void runCurrentStep();
    void evaluate(const QString& resultText, bool succeeded);

    void completeTask(const QString& answer);
    void failTask(const QString& reason);
    void requestFinalAnswer();

    void setAgentState(agent::AgentState state);
    [[nodiscard]] std::optional<QString> exhaustedLimit() const;
    void releaseTask();

    LlmController& m_llm;
    ToolCoordinator& m_tools;
    AiCoreModel& m_core;

    config::AgentSettings m_settings;
    agent::AgentStateMachine m_state;

    std::unique_ptr<agent::Task> m_task;
    std::unique_ptr<agent::Planner> m_planner;
    agent::MemoryStore m_memory;
    agent::RecoveryPolicy m_recovery;

    QString m_currentTool;
    QString m_lastError;
    int m_retries{0};
    int m_toolCalls{0};

    /// True while a generation this loop started is outstanding.
    bool m_awaitingGeneration{false};

    /// True while the model is being asked to phrase the final answer, so that
    /// generation is not mistaken for another tool round.
    bool m_finalising{false};

    /// Bumped by stop() and by every new task. Work carrying an older value has
    /// been abandoned and its result is dropped - this is what stops a late
    /// callback turning a cancelled task back into a running one.
    std::uint64_t m_generation{0};

    /// The generation the outstanding generation or tool call belongs to.
    ///
    /// Checking `m_task != nullptr` is not enough on its own: after a Stop, a
    /// *new* task can be under way by the time the abandoned work reports back,
    /// and without this the old result would be attributed to it. Every handler
    /// compares this against m_generation before doing anything.
    std::uint64_t m_workGeneration{0};
};

} // namespace jarvis::app
