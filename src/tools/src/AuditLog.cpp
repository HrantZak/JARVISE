#include "jarvis/tools/AuditLog.h"

#include <format>

namespace jarvis::tools {

std::string_view auditEventName(AuditEvent event) noexcept {
    switch (event) {
    case AuditEvent::ValidationAccepted:    return "VALIDATION_ACCEPTED";
    case AuditEvent::ValidationRejected:    return "VALIDATION_REJECTED";
    case AuditEvent::PermissionDenied:      return "PERMISSION_DENIED";
    case AuditEvent::ConfirmationRequested: return "CONFIRMATION_REQUESTED";
    case AuditEvent::ConfirmationAccepted:  return "CONFIRMATION_ACCEPTED";
    case AuditEvent::ConfirmationRejected:  return "CONFIRMATION_REJECTED";
    case AuditEvent::ConfirmationExpired:   return "CONFIRMATION_EXPIRED";
    case AuditEvent::ExecutionStarted:      return "EXECUTION_STARTED";
    case AuditEvent::ExecutionSucceeded:    return "EXECUTION_SUCCEEDED";
    case AuditEvent::ExecutionFailed:       return "EXECUTION_FAILED";
    case AuditEvent::ExecutionCancelled:    return "EXECUTION_CANCELLED";
    case AuditEvent::LoopLimitReached:      return "LOOP_LIMIT_REACHED";
    }
    return "VALIDATION_REJECTED";
}

std::string describeArguments(const ValidatedCall& call) {
    std::string text;
    for (const auto& [name, value] : call.arguments()) {
        if (!text.empty()) {
            text += ", ";
        }
        text += name;
        text += '=';
        switch (value.type) {
        case ArgumentType::Integer:
            text += std::to_string(value.integer);
            break;
        case ArgumentType::Boolean:
            text += value.boolean ? "true" : "false";
            break;
        case ArgumentType::Text:
        case ArgumentType::Enumeration:
            text += value.enumeration;
            break;
        }
    }
    return text;
}

std::string AuditRecord::toLine() const {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(timestamp);
    std::string line = std::format("{:%Y-%m-%d %H:%M:%S} [{}]", seconds,
                                   auditEventName(event));

    if (requestId != 0) {
        line += std::format(" #{}", requestId);
    }
    if (!toolName.empty()) {
        line += std::format(" tool={}", toolName);
    }
    if (!arguments.empty()) {
        line += std::format(" args=({})", arguments);
    }
    if (errorCode != ToolErrorCode::None) {
        line += std::format(" error={}", toolErrorName(errorCode));
    }
    if (duration.count() > 0) {
        line += std::format(" {}ms", duration.count());
    }
    if (!detail.empty()) {
        line += std::format(" - {}", detail);
    }

    return line;
}

AuditLog::AuditLog(std::size_t capacity)
    : m_capacity{capacity == 0 ? 1 : capacity} {
    m_records.reserve(m_capacity);
}

void AuditLog::append(AuditRecord record) {
    const std::lock_guard lock{m_mutex};

    ++m_total;
    if (m_records.size() >= m_capacity) {
        // Drop the oldest rather than refusing to record the newest: the recent
        // history is what anyone looking at this log needs.
        m_records.erase(m_records.begin());
        ++m_dropped;
    }
    m_records.push_back(std::move(record));
}

void AuditLog::record(AuditEvent event, std::uint64_t requestId, std::string toolName,
                      std::string detail) {
    AuditRecord entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.event = event;
    entry.requestId = requestId;
    entry.toolName = std::move(toolName);
    entry.detail = std::move(detail);
    append(std::move(entry));
}

std::vector<AuditRecord> AuditLog::records() const {
    const std::lock_guard lock{m_mutex};
    return m_records;
}

std::size_t AuditLog::size() const {
    const std::lock_guard lock{m_mutex};
    return m_records.size();
}

std::size_t AuditLog::droppedCount() const {
    const std::lock_guard lock{m_mutex};
    return m_dropped;
}

std::size_t AuditLog::totalCount() const {
    const std::lock_guard lock{m_mutex};
    return m_total;
}

void AuditLog::clear() {
    const std::lock_guard lock{m_mutex};
    m_records.clear();
    m_dropped = 0;
    m_total = 0;
}

} // namespace jarvis::tools
