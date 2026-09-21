#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <memory>
#include <vector>

#include "jarvis/agent/ContextManager.h"
#include "jarvis/config/AppConfig.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/i18n/LanguagePolicy.h"
#include "jarvis/llm/ILLMBackend.h"
#include "jarvis/llm/ModelRegistry.h"

namespace jarvis::app {

class AiCoreModel;
class ToolCoordinator;

/// The conversation, as the interface sees it.
class ConversationModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        RoleRole = Qt::UserRole + 1,   ///< "user" / "assistant"
        TextRole,
        TimestampRole,
        StreamingRole,                 ///< true while the answer is still arriving
    };

    explicit ConversationModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void appendUser(const QString& text);

    /// Opens an assistant turn that will be filled in token by token.
    void beginAssistant();
    void appendToAssistant(const QString& piece);

    /// Overwrites the open assistant turn. Used when the streamed text turns
    /// out to be a tool call: the raw JSON is machinery, not an answer, and
    /// showing it as one would misrepresent what the assistant said.
    void replaceAssistant(const QString& text);

    void endAssistant();

    Q_INVOKABLE void clear();

Q_SIGNALS:
    void countChanged();

private:
    struct Entry {
        QString role;
        QString text;
        QString timestamp;
        bool streaming{false};
    };

    std::vector<Entry> m_entries;
};

/// Drives the local model from QML.
///
/// Everything expensive - loading a model, generating tokens - runs on the
/// shared ThreadPool. Tokens come back through a queued invocation, so QML only
/// ever sees them on the GUI thread, and a 5 GB load never stalls a frame.
///
/// The controller owns the conversation and drives AiCoreModel, so the Core
/// remains the single source of visual state: Offline with no model, Idle with
/// one loaded, Thinking while generating.
class LlmController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY loadedChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY loadedChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY devicesChanged)
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadedChanged)
    Q_PROPERTY(QString loadedModelName READ loadedModelName NOTIFY loadedChanged)
    Q_PROPERTY(QString loadedModelPath READ loadedModelPath NOTIFY loadedChanged)
    Q_PROPERTY(int offloadedLayers READ offloadedLayers NOTIFY loadedChanged)
    Q_PROPERTY(int totalLayers READ totalLayers NOTIFY loadedChanged)
    Q_PROPERTY(int contextLength READ contextLength NOTIFY loadedChanged)
    Q_PROPERTY(bool gpuAccelerated READ gpuAccelerated NOTIFY loadedChanged)

    Q_PROPERTY(bool generating READ generating NOTIFY generatingChanged)
    Q_PROPERTY(bool reasoning READ reasoning NOTIFY generatingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastStats READ lastStats NOTIFY lastStatsChanged)
    Q_PROPERTY(ConversationModel* conversation READ conversation CONSTANT)

public:
    LlmController(core::ThreadPool& pool,
                  std::unique_ptr<llm::ILLMBackend> backend,
                  AiCoreModel& core,
                  QObject* parent = nullptr);
    ~LlmController() override;

    [[nodiscard]] bool available() const noexcept { return m_backend != nullptr; }
    [[nodiscard]] QString backendName() const;
    [[nodiscard]] QString deviceName() const { return m_deviceName; }
    [[nodiscard]] bool gpuAvailable() const noexcept { return m_gpuAvailable; }
    [[nodiscard]] QVariantList devices() const { return m_devices; }

    [[nodiscard]] QVariantList models() const { return m_models; }
    [[nodiscard]] bool scanning() const noexcept { return m_scanning; }

    [[nodiscard]] bool loaded() const;
    [[nodiscard]] bool loading() const noexcept { return m_loading; }
    [[nodiscard]] QString loadedModelName() const;
    [[nodiscard]] QString loadedModelPath() const;
    [[nodiscard]] int offloadedLayers() const;
    [[nodiscard]] int totalLayers() const;
    [[nodiscard]] int contextLength() const;
    [[nodiscard]] bool gpuAccelerated() const;

    [[nodiscard]] bool generating() const noexcept { return m_generating; }
    bool reasoning() const noexcept { return m_reasoning; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QString lastStats() const { return m_lastStats; }
    [[nodiscard]] ConversationModel* conversation() { return &m_conversation; }

    /// Settings used for the next load and the next generation.
    void applySettings(const config::LlmSettings& settings);
    bool replaceBackend(std::unique_ptr<llm::ILLMBackend> backend);

    /// Language the assistant answers in, taken from the interface language.
    void setLanguage(i18n::Language language);

    [[nodiscard]] i18n::Language language() const noexcept { return m_language; }

    /// Tokens the last generation produced, visible or not.
    ///
    /// A reasoning model can spend its whole budget inside a think block and
    /// emit nothing a person would see. That is a different failure from the
    /// model producing nothing at all, and the difference is worth telling the
    /// user - one is a setting, the other is a fault.
    [[nodiscard]] int lastGeneratedTokens() const noexcept {
        return m_lastGeneratedTokens;
    }

    /// The token budget the last generation was given.
    [[nodiscard]] int lastTokenBudget() const noexcept { return m_lastTokenBudget; }

    /// Extra text appended to the system prompt - the tool catalogue and the
    /// plan format, supplied by the agent. Documentation for the model; it
    /// grants nothing, and the boundary applies whether or not it is read.
    void setPromptSection(const QString& section);

    /// Starts a turn for \p text: records it, then generates.
    void generate(const QString& text);

    /// Another turn on the context as it stands, with no new user message.
    /// Used after a tool result has been added.
    void regenerate();

    /// Adds a tool result to the context, framed as data and size-capped.
    void appendToolResult(const QString& resultText);

    /// Adds a plain note for the model - a refusal, a limit that was reached.
    /// Data, like everything else that is not the system prompt.
    void appendNote(const QString& text);

    /// Replaces the open assistant turn in the visible transcript. Used when
    /// the streamed text turns out to be machinery - a tool call or a plan -
    /// which is not something the assistant "said".
    void replaceVisibleAnswer(const QString& text);
    void recordLocalExchange(const QString& request, const QString& answer);

    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void loadModel(const QString& path);
    Q_INVOKABLE void unloadModel();
    Q_INVOKABLE void send(const QString& text);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearConversation();

Q_SIGNALS:
    void devicesChanged();
    void modelsChanged();
    void scanningChanged();
    void loadedChanged();
    void generatingChanged();
    void lastErrorChanged();
    void lastStatsChanged();

    /// Emitted when a model finished loading, so the UI can react once.
    void modelLoaded(const QString& name);

    /// Visible text as it streams in, with the think-block already filtered.
    /// Added for the voice pipeline: the conversation view updates itself
    /// through the model, but VoiceController needs the text to segment it into
    /// sentences for synthesis.
    void assistantChunk(const QString& text);

    /// One generation ended. \p answer is the complete visible reply.
    void generationCompleted(const QString& answer, bool ok);

    /// The interface submitted a message. The agent owns what happens next;
    /// this class does not interpret it.
    void userMessageSubmitted(const QString& text);

private:
    void refreshDevices();
    void setError(const QString& message);
    void setGenerating(bool generating);
    void updateCoreState();

    /// Builds a request from the current context and starts generating.
    void startGeneration();

    /// Ends the turn: emits generationCompleted.
    void concludeTurn(const QString& answer, bool ok);

    /// Reassembles UTF-8 across token boundaries.
    ///
    /// The backend guarantees only that concatenating every piece yields valid
    /// UTF-8 - a single piece may stop half way through a character. Decoding
    /// pieces independently produced replacement characters in Russian text,
    /// because Cyrillic takes two bytes and token boundaries fall inside them
    /// constantly.
    class Utf8Assembler {
    public:
        /// Returns the text that is now complete. Holds back a trailing partial
        /// sequence until the rest of it arrives.
        [[nodiscard]] QString feed(std::string_view piece);

        /// Releases anything still held. Called when generation ends; a
        /// genuinely truncated sequence is dropped rather than shown as a
        /// broken character.
        [[nodiscard]] QString flush();

        void reset();

    private:
        std::string m_pending;
    };

    /// Writes the request that is about to be sent, when JARVIS_PROMPT_DUMP
    /// names a file. Off unless that variable is set, so it never runs for a
    /// user; it exists because diagnosing what the model was actually shown by
    /// reasoning about the code did not work.
    void dumpRequest(const llm::GenerationRequest& request) const;

    /// Suppresses the <think>...</think> preamble Qwen3-style models emit.
    /// Stateful because the markers can straddle two token boundaries.
    class ThinkFilter {
    public:
        [[nodiscard]] QString feed(QStringView piece);

        /// Releases text held back at the tail. Must be called when generation
        /// ends, or the last few characters of every answer are lost.
        [[nodiscard]] QString flush();

        void reset();

    private:
        QString m_pending;
        bool m_inThink{false};
    };

    core::ThreadPool& m_pool;
    std::unique_ptr<llm::ILLMBackend> m_backend;
    AiCoreModel& m_core;

    llm::ModelRegistry m_registry;
    ConversationModel m_conversation;

    /// The conversation as the model sees it, kept inside a token budget.
    ///
    /// Replaces the unbounded vector this used to be. That grew for the life of
    /// the session, and llama.cpp refuses a prompt longer than its window with
    /// ResourceExhausted - which a multi-step task, folding a tool result in
    /// after every step, reaches quickly.
    agent::ContextManager m_context;

    config::LlmSettings m_settings;
    i18n::Language m_language{i18n::kDefaultLanguage};

    QVariantList m_models;
    QVariantList m_devices;
    QString m_deviceName;
    QString m_lastError;
    QString m_lastStats;

    ThinkFilter m_thinkFilter;
    Utf8Assembler m_utf8Decoder;

    bool m_gpuAvailable{false};
    bool m_scanning{false};
    bool m_loading{false};
    bool m_generating{false};

    /// Set while a generation is running so cancel() can reach it.
    std::atomic<bool> m_busy{false};

    /// Appended to the system prompt by the agent.
    QString m_promptSection;

    int m_lastGeneratedTokens{0};
    QString m_latestUserRequest;
    int m_lastTokenBudget{512};
    bool m_reasoning{false};
};

} // namespace jarvis::app
