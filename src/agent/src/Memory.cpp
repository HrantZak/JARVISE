#include "jarvis/agent/Memory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <sstream>
#include <utility>

#include "jarvis/agent/ContextManager.h"

namespace jarvis::agent {
namespace {

std::string toLower(std::string_view text) {
    std::string lower;
    lower.reserve(text.size());
    for (const char c : text) {
        lower.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return lower;
}

bool isBase64ish(char c) {
    return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '+' || c == '/'
           || c == '-' || c == '_' || c == '=';
}

/// The longest run of characters that could be an encoded blob, and how varied
/// it is. A password is short and wordy; a key is long and mixes cases,
/// digits and symbols in a way prose does not.
struct Run {
    std::size_t length{0};
    bool hasUpper{false};
    bool hasLower{false};
    bool hasDigit{false};
};

Run longestOpaqueRun(std::string_view text) {
    Run best;
    Run current;

    const auto close = [&] {
        if (current.length > best.length) {
            best = current;
        }
        current = Run{};
    };

    for (const char c : text) {
        if (isBase64ish(c)) {
            ++current.length;
            if (std::isupper(static_cast<unsigned char>(c)) != 0) {
                current.hasUpper = true;
            } else if (std::islower(static_cast<unsigned char>(c)) != 0) {
                current.hasLower = true;
            } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                current.hasDigit = true;
            }
        } else {
            close();
        }
    }
    close();
    return best;
}

/// Words that label a credential when they are followed by one.
constexpr std::array<std::string_view, 14> kCredentialLabels{
    "password", "passwd", "пароль",  "api key",  "api_key",  "apikey", "token",
    "secret",   "secret_key", "private key", "credential", "auth",  "bearer",
    "ключ",
};

/// Prefixes real services use for keys. Present because shape alone would let
/// a short key through.
constexpr std::array<std::string_view, 8> kKeyPrefixes{
    "sk-", "sk_live_", "sk_test_", "pk_live_", "ghp_", "gho_", "xox", "aki",
};

} // namespace

std::string_view memoryScopeKey(MemoryScope scope) noexcept {
    switch (scope) {
    case MemoryScope::Session:    return "SESSION";
    case MemoryScope::Persistent: return "PERSISTENT";
    }
    return "SESSION";
}

std::string_view memoryCategoryKey(MemoryCategory category) noexcept {
    switch (category) {
    case MemoryCategory::Fact:        return "FACT";
    case MemoryCategory::Preference:  return "PREFERENCE";
    case MemoryCategory::TaskOutcome: return "TASK_OUTCOME";
    }
    return "FACT";
}

std::string_view memoryRefusalKey(MemoryPolicy::Refusal refusal) noexcept {
    switch (refusal) {
    case MemoryPolicy::Refusal::None:                 return "NONE";
    case MemoryPolicy::Refusal::Empty:                return "EMPTY";
    case MemoryPolicy::Refusal::TooLong:              return "TOO_LONG";
    case MemoryPolicy::Refusal::LooksLikeSecret:      return "LOOKS_LIKE_SECRET";
    case MemoryPolicy::Refusal::LooksLikeInstruction: return "LOOKS_LIKE_INSTRUCTION";
    }
    return "NONE";
}

// ---------------------------------------------------------------------------
// MemoryPolicy
// ---------------------------------------------------------------------------

MemoryPolicy::MemoryPolicy(Limits limits) {
    setLimits(limits);
}

void MemoryPolicy::setLimits(const Limits& limits) {
    m_limits.maxEntries = std::min(limits.maxEntries, kMaxEntriesCeiling);
    m_limits.maxEntryChars = std::min(limits.maxEntryChars, kMaxEntryCharsCeiling);
    m_limits.maxTotalChars = std::min(limits.maxTotalChars, kMaxTotalCharsCeiling);
}

bool MemoryPolicy::looksLikeSecret(std::string_view text) {
    const std::string lower = toLower(text);

    // A PEM block is unambiguous.
    if (lower.find("-----begin") != std::string::npos) {
        return true;
    }

    // Prefixes that real services mint keys with.
    for (const std::string_view prefix : kKeyPrefixes) {
        const std::size_t at = lower.find(prefix);
        if (at == std::string::npos) {
            continue;
        }
        // Followed by enough opaque characters to be a key rather than a word
        // that happens to start the same way.
        const Run run = longestOpaqueRun(lower.substr(at));
        if (run.length >= 16) {
            return true;
        }
    }

    // A JWT: three dot-separated base64 segments, the first starting "eyj".
    if (lower.find("eyj") != std::string::npos
        && std::ranges::count(lower, '.') >= 2) {
        return true;
    }

    // A connection string carrying a password.
    if (lower.find("://") != std::string::npos && lower.find(':') != std::string::npos
        && lower.find('@') != std::string::npos
        && (lower.find("password") != std::string::npos
            || lower.find("pwd=") != std::string::npos)) {
        return true;
    }

    // A labelled credential: the word, then a separator, then something opaque.
    for (const std::string_view label : kCredentialLabels) {
        const std::size_t at = lower.find(label);
        if (at == std::string::npos) {
            continue;
        }

        const std::string_view rest{lower.data() + at + label.size(),
                                    lower.size() - at - label.size()};
        // The label has to be introducing a value, not merely mentioned. "the
        // password field was empty" is prose; "password: hunter2" is not.
        const std::size_t separator = rest.find_first_of(":=");
        if (separator == std::string::npos || separator > 4) {
            continue;
        }

        const Run run = longestOpaqueRun(rest.substr(separator));
        if (run.length >= 6) {
            return true;
        }
    }

    // A long, varied, opaque run with no spaces in it. Nobody writes this by
    // hand, and prose does not produce it.
    const Run run = longestOpaqueRun(text);
    const int variety = static_cast<int>(run.hasUpper) + static_cast<int>(run.hasLower)
                        + static_cast<int>(run.hasDigit);
    return run.length >= 24 && variety >= 2;
}

bool MemoryPolicy::looksLikeInstruction(std::string_view text) {
    const std::string lower = toLower(text);

    // Phrases whose only purpose is to address the model or claim authority.
    // This is not the security boundary - memory is framed as data and grants
    // nothing - it is housekeeping: there is no reason to keep such text, so
    // it does not get kept.
    static constexpr std::array<std::string_view, 12> kPhrases{
        "ignore previous", "ignore all previous", "disregard previous",
        "you are now",     "system:",             "system message",
        "developer:",      "confirmed=true",      "confirmed: true",
        "permission=",     "skip_confirmation",   "you are administrator",
    };

    return std::ranges::any_of(kPhrases, [&lower](std::string_view phrase) {
        return lower.find(phrase) != std::string::npos;
    });
}

MemoryPolicy::Decision MemoryPolicy::evaluate(std::string_view content,
                                              MemoryScope scope) const {
    static_cast<void>(scope);

    if (content.empty()
        || content.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        return Decision{false, Refusal::Empty, "there is nothing to remember"};
    }
    if (content.size() > m_limits.maxEntryChars) {
        return Decision{false, Refusal::TooLong,
                        std::format("longer than the {} character limit",
                                    m_limits.maxEntryChars)};
    }
    if (looksLikeSecret(content)) {
        return Decision{false, Refusal::LooksLikeSecret,
                        "it looks like a credential"};
    }
    if (looksLikeInstruction(content)) {
        return Decision{false, Refusal::LooksLikeInstruction,
                        "it reads as an instruction rather than a fact"};
    }

    return Decision{true, Refusal::None, {}};
}

// ---------------------------------------------------------------------------
// MemoryStore
// ---------------------------------------------------------------------------

MemoryStore::MemoryStore(Config config) {
    setConfig(config);
}

void MemoryStore::setConfig(const Config& config) {
    m_config = config;
    m_policy.setLimits(config.limits);
    m_config.limits = m_policy.limits();

    if (!m_config.persistent) {
        // Persistence was turned off. Anything already marked persistent
        // becomes session-only rather than being kept against the setting.
        for (MemoryEntry& entry : m_entries) {
            entry.scope = MemoryScope::Session;
        }
    }
    enforceLimits();
}

std::optional<MemoryEntry> MemoryStore::remember(std::string content,
                                                 MemoryCategory category,
                                                 MemoryScope scope, int importance,
                                                 MemoryPolicy::Refusal* refusal) {
    if (refusal != nullptr) {
        *refusal = MemoryPolicy::Refusal::None;
    }
    if (!m_config.enabled) {
        return std::nullopt;
    }

    const MemoryPolicy::Decision decision = m_policy.evaluate(content, scope);
    if (!decision) {
        if (refusal != nullptr) {
            *refusal = decision.refusal;
        }
        return std::nullopt;
    }

    // Asking for persistence when it is switched off gets a session memory, not
    // a refusal: the agent still works, and nothing is written where the user
    // said it should not be.
    const MemoryScope effective =
        (scope == MemoryScope::Persistent && m_config.persistent) ? MemoryScope::Persistent
                                                                  : MemoryScope::Session;

    const auto now = std::chrono::system_clock::now();

    MemoryEntry entry;
    entry.id = m_nextId++;
    entry.createdAt = now;
    entry.updatedAt = now;
    entry.category = category;
    entry.scope = effective;
    entry.content = std::move(content);
    entry.importance = std::clamp(importance, 0, 100);

    m_entries.push_back(entry);
    enforceLimits();

    return find(entry.id).has_value() ? std::optional{entry} : std::nullopt;
}

void MemoryStore::enforceLimits() {
    const auto rank = [](const MemoryEntry& a, const MemoryEntry& b) {
        if (a.importance != b.importance) {
            return a.importance > b.importance;
        }
        // Ties by recency, then by id. Fully ordered, so eviction is
        // reproducible rather than dependent on how the vector happened to sit.
        if (a.updatedAt != b.updatedAt) {
            return a.updatedAt > b.updatedAt;
        }
        return a.id > b.id;
    };

    std::ranges::stable_sort(m_entries, rank);

    while (m_entries.size() > m_config.limits.maxEntries) {
        m_entries.pop_back();
        ++m_evicted;
    }

    while (totalChars() > m_config.limits.maxTotalChars && !m_entries.empty()) {
        m_entries.pop_back();
        ++m_evicted;
    }
}

std::vector<MemoryEntry> MemoryStore::recall(std::size_t maximum) const {
    if (!m_config.enabled) {
        return {};
    }
    // Already in rank order.
    std::vector<MemoryEntry> result;
    result.reserve(std::min(maximum, m_entries.size()));
    for (std::size_t i = 0; i < m_entries.size() && i < maximum; ++i) {
        result.push_back(m_entries[i]);
    }
    return result;
}

std::optional<MemoryEntry> MemoryStore::find(std::uint64_t id) const {
    const auto it = std::ranges::find_if(
        m_entries, [id](const MemoryEntry& entry) { return entry.id == id; });
    return it != m_entries.end() ? std::optional{*it} : std::nullopt;
}

bool MemoryStore::forget(std::uint64_t id) {
    const auto removed = std::ranges::remove_if(
        m_entries, [id](const MemoryEntry& entry) { return entry.id == id; });
    if (removed.begin() == m_entries.end()) {
        return false;
    }
    m_entries.erase(removed.begin(), removed.end());
    return true;
}

void MemoryStore::clear() {
    m_entries.clear();
    m_evicted = 0;
}

std::vector<MemoryEntry> MemoryStore::persistentEntries() const {
    std::vector<MemoryEntry> result;
    for (const MemoryEntry& entry : m_entries) {
        if (entry.scope == MemoryScope::Persistent) {
            result.push_back(entry);
        }
    }
    return result;
}

std::size_t MemoryStore::totalChars() const {
    std::size_t total = 0;
    for (const MemoryEntry& entry : m_entries) {
        total += entry.content.size();
    }
    return total;
}

std::string MemoryStore::promptSection(std::size_t maximum) const {
    const std::vector<MemoryEntry> entries = recall(maximum);
    if (entries.empty()) {
        return {};
    }

    std::string body;
    for (const MemoryEntry& entry : entries) {
        body += std::format("- [{}] {}\n", memoryCategoryKey(entry.category),
                            entry.content);
    }

    // Through the same framing tool results use. Memory is data: it is never
    // placed in the system prompt, and the block says so in words the model
    // reads.
    return frameMemory(body);
}

std::string MemoryStore::serialise() const {
    // One entry per line, tab-separated, content last so a tab inside it cannot
    // shift the other fields. Newlines are escaped for the same reason.
    std::string text;
    for (const MemoryEntry& entry : m_entries) {
        if (entry.scope != MemoryScope::Persistent) {
            continue;
        }

        std::string escaped;
        escaped.reserve(entry.content.size());
        for (const char c : entry.content) {
            if (c == '\n') {
                escaped += "\\n";
            } else if (c == '\t') {
                escaped += "\\t";
            } else if (c == '\\') {
                escaped += "\\\\";
            } else {
                escaped.push_back(c);
            }
        }

        text += std::format("{}\t{}\t{}\n", memoryCategoryKey(entry.category),
                            entry.importance, escaped);
    }
    return text;
}

std::size_t MemoryStore::deserialise(std::string_view text) {
    std::size_t accepted = 0;
    std::istringstream stream{std::string{text}};
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        const std::size_t firstTab = line.find('\t');
        const std::size_t secondTab =
            firstTab == std::string::npos ? std::string::npos
                                          : line.find('\t', firstTab + 1);
        if (secondTab == std::string::npos) {
            // Malformed. Skipped rather than aborting the load: one bad byte in
            // the file must not cost the user everything they had.
            continue;
        }

        const std::string categoryKey = line.substr(0, firstTab);
        MemoryCategory category = MemoryCategory::Fact;
        if (categoryKey == "PREFERENCE") {
            category = MemoryCategory::Preference;
        } else if (categoryKey == "TASK_OUTCOME") {
            category = MemoryCategory::TaskOutcome;
        } else if (categoryKey != "FACT") {
            continue;
        }

        int importance = 50;
        try {
            importance = std::stoi(line.substr(firstTab + 1, secondTab - firstTab - 1));
        } catch (const std::exception&) {
            continue;
        }

        std::string content;
        const std::string raw = line.substr(secondTab + 1);
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '\\' || i + 1 >= raw.size()) {
                content.push_back(raw[i]);
                continue;
            }
            ++i;
            switch (raw[i]) {
            case 'n':  content.push_back('\n'); break;
            case 't':  content.push_back('\t'); break;
            case '\\': content.push_back('\\'); break;
            default:   content.push_back(raw[i]); break;
            }
        }

        // Through the policy again. A file edited by hand cannot introduce
        // something the policy would have refused when it was written.
        if (remember(std::move(content), category, MemoryScope::Persistent,
                     importance)) {
            ++accepted;
        }
    }

    return accepted;
}

} // namespace jarvis::agent
