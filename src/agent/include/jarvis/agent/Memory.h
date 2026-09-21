#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jarvis::agent {

/// How long a memory is meant to last.
enum class MemoryScope {
    /// Forgotten when the application closes.
    Session,

    /// Survives a restart. Opt-in, and never the default: anything kept here
    /// becomes input to future prompts, which is a decision the user makes.
    Persistent,
};

[[nodiscard]] std::string_view memoryScopeKey(MemoryScope scope) noexcept;

/// What kind of thing is being remembered. An enumeration rather than free
/// text, so the store can be reasoned about without reading its contents.
enum class MemoryCategory {
    /// Something about the machine or the user that does not change often.
    Fact,

    /// How the user likes things done.
    Preference,

    /// What happened in an earlier task.
    TaskOutcome,
};

[[nodiscard]] std::string_view memoryCategoryKey(MemoryCategory category) noexcept;

/// One remembered thing.
struct MemoryEntry {
    std::uint64_t id{0};
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;

    MemoryCategory category{MemoryCategory::Fact};
    MemoryScope scope{MemoryScope::Session};

    /// The text itself. **Untrusted.** It is shown to the model inside an
    /// `<untrusted_memory>` block and never as a system message.
    std::string content;

    /// 0-100. Ties are broken by recency, so eviction is deterministic.
    int importance{50};

    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

/// Decides what may be written down at all.
///
/// The rule this exists to enforce: **the model cannot ask for something to be
/// remembered.** Nothing in a tool result or a reply reaches this class as an
/// instruction; the application decides to record something, and then this
/// decides whether it is allowed to.
///
/// Secret detection is a layer, not a keyword list. A single word would be both
/// too eager - "password" appears in ordinary sentences - and far too easy to
/// step around. The checks below look at shape: labelled credentials, key
/// prefixes real services use, PEM blocks, JWTs, connection strings, and long
/// high-entropy runs that no human wrote by hand.
class MemoryPolicy {
public:
    struct Limits {
        std::size_t maxEntries{64};
        std::size_t maxEntryChars{512};
        std::size_t maxTotalChars{32768};
    };

    /// Hard ceilings. Configuration may lower these; nothing raises them.
    static constexpr std::size_t kMaxEntriesCeiling = 1024;
    static constexpr std::size_t kMaxEntryCharsCeiling = 4096;
    static constexpr std::size_t kMaxTotalCharsCeiling = 262144;

    enum class Refusal {
        None,
        Empty,
        TooLong,
        LooksLikeSecret,
        LooksLikeInstruction,
    };

    struct Decision {
        bool allowed{false};
        Refusal refusal{Refusal::None};
        std::string reason;

        explicit operator bool() const noexcept { return allowed; }
    };

    explicit MemoryPolicy(Limits limits = {});

    void setLimits(const Limits& limits);
    [[nodiscard]] const Limits& limits() const noexcept { return m_limits; }

    /// Whether \p content may be stored.
    [[nodiscard]] Decision evaluate(std::string_view content, MemoryScope scope) const;

    /// True when the text carries the shape of a credential. Exposed so the
    /// tests can state the property directly rather than through the store.
    [[nodiscard]] static bool looksLikeSecret(std::string_view text);

    /// True when the text is trying to sound like an instruction to the model
    /// or the system. Such text is not *dangerous* on its own - memory is
    /// framed as data and carries no authority - but there is no legitimate
    /// reason to keep it, and refusing costs nothing.
    [[nodiscard]] static bool looksLikeInstruction(std::string_view text);

private:
    Limits m_limits;
};

[[nodiscard]] std::string_view memoryRefusalKey(MemoryPolicy::Refusal refusal) noexcept;

/// What the agent remembers between tasks.
///
/// Qt-free and free of the model layer. Bounded in three directions - entries,
/// entry size, total size - because an unbounded store is both a memory leak
/// and a way to push everything else out of the context window.
///
/// **Not the context.** ContextManager holds the current conversation and is
/// rebuilt constantly; this holds a few durable facts and is consulted
/// deliberately.
class MemoryStore {
public:
    struct Config {
        /// Master switch. Off means nothing is stored and nothing is recalled.
        bool enabled{false};

        /// Whether Persistent entries are kept at all. With this off, a request
        /// to store one is downgraded to Session rather than refused, so the
        /// agent still works and nothing is written where it should not be.
        bool persistent{false};

        MemoryPolicy::Limits limits;
    };

    explicit MemoryStore(Config config = {});

    void setConfig(const Config& config);
    [[nodiscard]] const Config& config() const noexcept { return m_config; }

    /// Records something, if the policy allows it.
    ///
    /// Returns the stored entry, or nothing with the reason in \p refusal.
    /// Storing is always the application's decision: there is no path from a
    /// model's output to this function that does not pass through code that
    /// decided to call it.
    std::optional<MemoryEntry> remember(std::string content, MemoryCategory category,
                                        MemoryScope scope = MemoryScope::Session,
                                        int importance = 50,
                                        MemoryPolicy::Refusal* refusal = nullptr);

    /// The most relevant entries, most important first, then most recent.
    /// Deterministic: the same store always returns the same order.
    [[nodiscard]] std::vector<MemoryEntry> recall(std::size_t maximum = 8) const;

    [[nodiscard]] std::optional<MemoryEntry> find(std::uint64_t id) const;
    bool forget(std::uint64_t id);
    void clear();

    /// Only the entries that survive a restart.
    [[nodiscard]] std::vector<MemoryEntry> persistentEntries() const;

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }
    [[nodiscard]] std::size_t totalChars() const;

    /// How many entries were evicted to stay inside the limits.
    [[nodiscard]] std::size_t evictedCount() const noexcept { return m_evicted; }

    /// The block handed to the model, already framed as untrusted data. Empty
    /// when memory is off or there is nothing to say.
    [[nodiscard]] std::string promptSection(std::size_t maximum = 8) const;

    // --- persistence ------------------------------------------------------

    /// Serialises the persistent entries. Plain, line-oriented and easy to
    /// inspect: anything kept between sessions should be readable by the person
    /// it belongs to.
    [[nodiscard]] std::string serialise() const;

    /// Loads entries produced by serialise().
    ///
    /// Every entry is put through the policy again on the way in. A file edited
    /// by hand, or corrupted, cannot introduce something the policy would have
    /// refused - and a malformed line is skipped rather than aborting the load,
    /// so one bad byte does not cost the user everything they had.
    ///
    /// Returns how many entries were accepted.
    std::size_t deserialise(std::string_view text);

private:
    void enforceLimits();

    Config m_config;
    MemoryPolicy m_policy;
    std::vector<MemoryEntry> m_entries;
    std::uint64_t m_nextId{1};
    std::size_t m_evicted{0};
};

} // namespace jarvis::agent
