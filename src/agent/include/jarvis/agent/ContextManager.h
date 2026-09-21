#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jarvis::agent {

/// Who said it.
///
/// Deliberately not `llm::ChatMessage::Role`: this module stays free of the
/// language-model layer, so the agent's context can be reasoned about and
/// tested without a backend. The application converts at the boundary.
enum class ContextRole {
    System,
    User,
    Assistant,

    /// Data returned by a tool. A separate role because it is framed
    /// differently and is never allowed to carry authority.
    ToolResult,
};

[[nodiscard]] std::string_view contextRoleKey(ContextRole role) noexcept;

/// How badly an entry is needed when the budget runs out.
enum class ContextPriority {
    /// Never dropped. Security rules, the task's identity, the active step.
    /// If Critical alone does not fit, the request is refused rather than
    /// trimmed - losing this silently is how an agent forgets which task it is
    /// working on while still acting.
    Critical,

    /// The current request, the plan, results the next step depends on.
    High,

    /// Recent conversation.
    Normal,

    /// Older turns and results nothing depends on any more.
    Low,
};

[[nodiscard]] std::string_view contextPriorityKey(ContextPriority priority) noexcept;

/// One thing in the context.
struct ContextEntry {
    ContextRole role{ContextRole::User};
    ContextPriority priority{ContextPriority::Normal};
    std::string content;

    /// Insertion order. Compaction keeps the newest of an equal priority, and
    /// the order is what makes that decision reproducible.
    std::uint64_t sequence{0};

    /// True when the content was cut down on the way in. Never silent: the
    /// text carries a marker and this flag records it.
    bool truncated{false};

    [[nodiscard]] std::size_t estimatedTokens() const;
};

/// What is actually sent, after compaction.
struct PreparedContext {
    std::vector<ContextEntry> entries;

    std::size_t estimatedTokens{0};

    /// How many entries compaction removed, and how many tokens that saved.
    std::size_t droppedEntries{0};

    /// True when even the Critical entries did not fit. The caller must not
    /// send this: it means the budget is impossible, not that the context is
    /// merely tight.
    bool overBudget{false};
};

/// Keeps the conversation inside a fixed budget, deterministically.
///
/// The problem this exists for is concrete: history used to grow without limit,
/// and llama.cpp refuses a prompt longer than its context with
/// `ResourceExhausted`. For a one-shot chat that is a rare annoyance; for an
/// agent taking six steps and folding a tool result in after each one, it is a
/// wall the task hits mid-flight.
///
/// **This class decides what to send. It decides nothing else.** It does not
/// run tools, evaluate permissions, confirm actions, call a planner, touch the
/// interface or persist anything. Memory between tasks is a different component
/// with different rules.
///
/// Qt-free and free of the LLM layer, so the compaction rules can be tested
/// exhaustively without a model.
class ContextManager {
public:
    /// Hard ceiling on a single tool result, in characters.
    ///
    /// 4096 is about a page of text - far more than any tool here produces, and
    /// small enough that a tool returning a megabyte cannot evict the task's
    /// own identity from the context. Configuration may lower it; nothing can
    /// raise it past this.
    static constexpr std::size_t kMaxToolResultChars = 4096;

    /// Hard ceiling on the number of entries kept, before budgeting. Stops a
    /// runaway loop growing the vector itself without bound.
    static constexpr std::size_t kMaxEntries = 512;

    struct Budget {
        /// The model's context window, in tokens.
        std::size_t contextTokens{8192};

        /// Held back for the answer. A prompt that exactly fills the window
        /// leaves no room to reply.
        std::size_t outputReserveTokens{768};

        /// Applied to each tool result as it arrives. Clamped to
        /// kMaxToolResultChars.
        std::size_t maxToolResultChars{kMaxToolResultChars};

        /// Tokens available for the prompt.
        [[nodiscard]] std::size_t promptTokens() const noexcept;
    };

    ContextManager() = default;
    explicit ContextManager(Budget budget);

    void setBudget(const Budget& budget);
    [[nodiscard]] const Budget& budget() const noexcept { return m_budget; }

    /// A rough token count for \p text.
    ///
    /// An estimate, and deliberately a pessimistic one: three bytes per token.
    /// Latin text runs nearer four, Russian nearer two because Cyrillic takes
    /// two bytes per character in UTF-8 and tokenises poorly. Guessing low
    /// would mean discovering the real limit as a failure from the backend, so
    /// this guesses high and the reserve absorbs the rest.
    [[nodiscard]] static std::size_t estimateTokens(std::string_view text);

    // --- adding -----------------------------------------------------------

    /// Replaces the system prompt. Always Critical, always first.
    void setSystemPrompt(std::string content);

    void addUser(std::string content, ContextPriority priority = ContextPriority::High);
    void addAssistant(std::string content,
                      ContextPriority priority = ContextPriority::Normal);

    /// Adds a tool result, truncating it to the budget's limit.
    ///
    /// Returns true when the text was cut. The caller is expected to say so
    /// rather than let the model believe it saw everything.
    bool addToolResult(std::string content,
                       ContextPriority priority = ContextPriority::High);

    /// Facts about the task in progress: its identity, the active step. Always
    /// Critical, and replaced rather than appended so the context does not fill
    /// with the history of a single task's progress.
    void setTaskContext(std::string content);
    void clearTaskContext();

    // --- reading ----------------------------------------------------------

    /// The entries that fit, oldest first, after dropping the least important.
    [[nodiscard]] PreparedContext prepare() const;

    [[nodiscard]] const std::vector<ContextEntry>& entries() const noexcept {
        return m_entries;
    }
    [[nodiscard]] std::size_t entryCount() const noexcept { return m_entries.size(); }

    /// Total estimated tokens of everything held, before compaction.
    [[nodiscard]] std::size_t estimatedTokens() const;

    /// Forgets the conversation. The system prompt and task context survive,
    /// because they are not conversation.
    void clearConversation();

    void clear();

private:
    void append(ContextEntry entry);
    void enforceEntryCeiling();

    Budget m_budget;
    std::string m_systemPrompt;
    std::string m_taskContext;
    std::vector<ContextEntry> m_entries;
    std::uint64_t m_nextSequence{1};
};

/// Wraps tool output as data, for the model.
///
/// The framing is part of the security story rather than politeness: a result
/// arrives as something the model is being *shown*, never as something it is
/// being *told*. Text inside it that reads like an instruction - "ignore
/// previous instructions", a forged system message, a claimed approval - is
/// still just a value inside a marked block, and it is never placed in the
/// system prompt where text carries authority.
[[nodiscard]] std::string frameToolResult(std::string_view resultText);

/// The same treatment for anything recalled from memory.
[[nodiscard]] std::string frameMemory(std::string_view memoryText);

} // namespace jarvis::agent
