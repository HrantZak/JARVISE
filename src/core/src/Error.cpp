#include "jarvis/core/Error.h"

#include <filesystem>

namespace jarvis::core {
namespace {

/// Keep only the file name; absolute build paths are noise in a log line.
std::string basename(const char* path) {
    if (path == nullptr) {
        return {};
    }
    return std::filesystem::path{path}.filename().string();
}

} // namespace

std::string_view toStringView(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Unknown:           return "Unknown";
    case ErrorCode::InvalidArgument:   return "InvalidArgument";
    case ErrorCode::NotFound:          return "NotFound";
    case ErrorCode::AlreadyExists:     return "AlreadyExists";
    case ErrorCode::PermissionDenied:  return "PermissionDenied";
    case ErrorCode::IoFailure:         return "IoFailure";
    case ErrorCode::ParseFailure:      return "ParseFailure";
    case ErrorCode::NotImplemented:    return "NotImplemented";
    case ErrorCode::Cancelled:         return "Cancelled";
    case ErrorCode::Timeout:           return "Timeout";
    case ErrorCode::ResourceExhausted: return "ResourceExhausted";
    case ErrorCode::Unavailable:       return "Unavailable";
    case ErrorCode::InternalFailure:   return "InternalFailure";
    }
    return "Unknown";
}

Error::Error(ErrorCode code, std::string message, const std::source_location& location)
    : m_code{code}
    , m_message{std::move(message)}
    , m_file{basename(location.file_name())}
    , m_function{location.function_name() != nullptr ? location.function_name() : ""}
    , m_line{location.line()} {}

std::string Error::toString() const {
    std::string out;
    out.reserve(m_message.size() + m_file.size() + 32);
    out.append(toStringView(m_code));
    out.append(": ");
    out.append(m_message);
    if (!m_file.empty()) {
        out.append(" (");
        out.append(m_file);
        out.push_back(':');
        out.append(std::to_string(m_line));
        out.push_back(')');
    }
    return out;
}

std::string Error::toUserString() const {
    std::string out;
    out.reserve(m_message.size() + 24);
    out.append(toStringView(m_code));
    out.append(": ");
    out.append(m_message);
    return out;
}

} // namespace jarvis::core
