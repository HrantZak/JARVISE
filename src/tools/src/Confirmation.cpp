#include "jarvis/tools/Confirmation.h"

#include <algorithm>
#include <format>

namespace jarvis::tools {

std::string callFingerprint(const ValidatedCall& call) {
    // std::map iterates in key order, so the same call always produces the same
    // text regardless of the order the arguments arrived in. The type is part
    // of each entry: `level=1` as an integer and `level=1` as an enumeration
    // are different calls and must not share a fingerprint.
    std::string text = call.toolName();
    text += '\0';

    for (const auto& [name, value] : call.arguments()) {
        text += name;
        text += '=';
        switch (value.type) {
        case ArgumentType::Text:
            text += "t:";
            text += value.enumeration;
            break;
        case ArgumentType::Integer:
            text += std::format("i:{}", value.integer);
            break;
        case ArgumentType::Boolean:
            text += value.boolean ? "b:true" : "b:false";
            break;
        case ArgumentType::Enumeration:
            text += "e:";
            text += value.enumeration;
            break;
        }
        text += '\0';
    }

    return text;
}

std::string scopedFingerprint(std::string_view scope, const ValidatedCall& call) {
    // The scope comes first and is terminated by a null, so a scope ending in
    // text that looks like a tool name cannot be confused with the call itself.
    std::string text{scope};
    text += '\0';
    text += callFingerprint(call);
    return text;
}

std::uint64_t ConfirmationStore::createRequest(const ValidatedCall& call,
                                               std::chrono::milliseconds timeout,
                                               std::string_view scope,
                                               Clock::time_point now) {
    Pending pending;
    pending.id = m_nextId++;
    pending.toolName = call.toolName();
    pending.fingerprint = scopedFingerprint(scope, call);
    pending.created = now;
    pending.expires = now + timeout;

    m_pending.push_back(std::move(pending));
    return m_pending.back().id;
}

std::optional<ConfirmationGrant> ConfirmationStore::approve(std::uint64_t requestId,
                                                            Clock::time_point now) {
    const auto it = std::ranges::find_if(
        m_pending, [requestId](const Pending& p) { return p.id == requestId; });

    if (it == m_pending.end()) {
        // Unknown, or already spent. Both mean the same thing here: there is no
        // outstanding request with this id, so there is nothing to approve.
        return std::nullopt;
    }

    // Removed whether or not it is still valid. An expired request must not
    // linger where a later call could approve it, and a spent one must not be
    // approvable twice.
    const Pending pending = *it;
    m_pending.erase(it);

    if (now >= pending.expires) {
        return std::nullopt;
    }

    ConfirmationGrant grant;
    grant.m_requestId = pending.id;
    grant.m_fingerprint = pending.fingerprint;
    return grant;
}

bool ConfirmationStore::reject(std::uint64_t requestId) {
    const auto it = std::ranges::find_if(
        m_pending, [requestId](const Pending& p) { return p.id == requestId; });
    if (it == m_pending.end()) {
        return false;
    }
    m_pending.erase(it);
    return true;
}

std::vector<std::uint64_t> ConfirmationStore::expire(Clock::time_point now) {
    std::vector<std::uint64_t> lapsed;

    const auto removed = std::ranges::remove_if(m_pending, [&](const Pending& p) {
        if (now >= p.expires) {
            lapsed.push_back(p.id);
            return true;
        }
        return false;
    });
    m_pending.erase(removed.begin(), removed.end());

    return lapsed;
}

std::optional<ConfirmationStore::Pending> ConfirmationStore::find(
    std::uint64_t requestId) const {
    const auto it = std::ranges::find_if(
        m_pending, [requestId](const Pending& p) { return p.id == requestId; });
    return it != m_pending.end() ? std::optional{*it} : std::nullopt;
}

} // namespace jarvis::tools
