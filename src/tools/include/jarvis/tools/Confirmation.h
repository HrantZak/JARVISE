#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jarvis/tools/ToolTypes.h"

namespace jarvis::tools {

/// A canonical, comparable description of exactly one call.
///
/// Built from the tool name and every validated argument, in a fixed order. Two
/// calls share a fingerprint only if they are the same call, so a confirmation
/// granted for "open notepad" cannot be spent on "open explorer".
[[nodiscard]] std::string callFingerprint(const ValidatedCall& call);

/// A fingerprint that also names where the call sits.
///
/// \p scope identifies the task and step the call belongs to; the agent builds
/// it with `agent::stepIdentity()`. An empty scope means a call made outside
/// any task, which is how a single tool call from a plain conversation works.
///
/// Binding the scope in is what stops an approval being replayed: the same
/// tool with the same arguments, at a different step or in a different task,
/// is a different thing to approve. The scope is kept as an opaque string so
/// this module never has to know what a task is.
[[nodiscard]] std::string scopedFingerprint(std::string_view scope,
                                            const ValidatedCall& call);

/// Proof that a human approved one specific call.
///
/// Move-only and produced only by ConfirmationStore::approve(). There is no
/// public constructor, no way to copy one, and no field a model could set: the
/// type system, not a runtime check, is what stops a forged approval. A JSON
/// payload containing `"confirmed": true` cannot become one of these, because
/// nothing anywhere converts data into a grant.
class ConfirmationGrant {
public:
    ConfirmationGrant(const ConfirmationGrant&) = delete;
    ConfirmationGrant& operator=(const ConfirmationGrant&) = delete;
    ConfirmationGrant(ConfirmationGrant&&) noexcept = default;
    ConfirmationGrant& operator=(ConfirmationGrant&&) noexcept = default;
    ~ConfirmationGrant() = default;

    [[nodiscard]] std::uint64_t requestId() const noexcept { return m_requestId; }

    /// The call this grant is for. The executor compares it against the call it
    /// is about to run, so approving one call and submitting another fails.
    [[nodiscard]] const std::string& fingerprint() const noexcept {
        return m_fingerprint;
    }

private:
    friend class ConfirmationStore;
    ConfirmationGrant() = default;

    std::uint64_t m_requestId{0};
    std::string m_fingerprint;
};

/// Pending confirmation requests.
///
/// Qt-free on purpose: the rules about identity, expiry and single use are
/// testable without an event loop, and there is no path from QML that reaches
/// past them. The Qt layer (app::ConfirmationManager) owns one of these and
/// drives the dialog; it can only ask this class to approve a request that this
/// class itself created.
class ConfirmationStore {
public:
    using Clock = std::chrono::steady_clock;

    struct Pending {
        std::uint64_t id{0};
        std::string toolName;
        std::string fingerprint;
        Clock::time_point created;
        Clock::time_point expires;
    };

    /// Records a request for approval and returns its id. The id is unique for
    /// the lifetime of the process and never reused, so a stale id cannot
    /// collide with a later request.
    ///
    /// \p scope binds the approval to one place in one task. Leave it empty for
    /// a call that belongs to no task.
    std::uint64_t createRequest(const ValidatedCall& call,
                                std::chrono::milliseconds timeout,
                                std::string_view scope = {},
                                Clock::time_point now = Clock::now());

    /// Approves a request. Called only by the application layer, and only in
    /// response to a real interaction.
    ///
    /// Returns nullopt when the id is unknown, already spent, or expired. The
    /// request is removed either way, so a second approve() of the same id
    /// fails - a grant is one-shot by construction.
    [[nodiscard]] std::optional<ConfirmationGrant> approve(
        std::uint64_t requestId, Clock::time_point now = Clock::now());

    /// Withdraws a request. Returns false if there was nothing to withdraw.
    bool reject(std::uint64_t requestId);

    /// Drops every request whose deadline has passed and returns their ids, so
    /// the caller can tell the user and the audit log what lapsed.
    std::vector<std::uint64_t> expire(Clock::time_point now = Clock::now());

    [[nodiscard]] std::optional<Pending> find(std::uint64_t requestId) const;
    [[nodiscard]] std::size_t pendingCount() const noexcept { return m_pending.size(); }

    void clear() { m_pending.clear(); }

private:
    std::uint64_t m_nextId{1};
    std::vector<Pending> m_pending;
};

} // namespace jarvis::tools
