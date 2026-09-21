#include "jarvis/tools/ToolTypes.h"

#include <algorithm>
#include <format>
#include <utility>

namespace jarvis::tools {

std::string_view permissionName(PermissionLevel level) noexcept {
    switch (level) {
    case PermissionLevel::ReadOnly:        return "READ_ONLY";
    case PermissionLevel::SafeAction:      return "SAFE_ACTION";
    case PermissionLevel::ConfirmRequired: return "CONFIRM_REQUIRED";
    case PermissionLevel::Destructive:     return "DESTRUCTIVE";
    case PermissionLevel::Denied:          return "DENIED";
    }
    return "DENIED";
}

std::string_view toolErrorName(ToolErrorCode code) noexcept {
    switch (code) {
    case ToolErrorCode::None:                    return "NONE";
    case ToolErrorCode::MalformedJson:           return "MALFORMED_JSON";
    case ToolErrorCode::NotAnObject:             return "NOT_AN_OBJECT";
    case ToolErrorCode::MissingToolField:        return "MISSING_TOOL_FIELD";
    case ToolErrorCode::UnknownTool:             return "UNKNOWN_TOOL";
    case ToolErrorCode::MissingArguments:        return "MISSING_ARGUMENTS";
    case ToolErrorCode::ArgumentsNotAnObject:    return "ARGUMENTS_NOT_AN_OBJECT";
    case ToolErrorCode::UnknownArgument:         return "UNKNOWN_ARGUMENT";
    case ToolErrorCode::MissingRequiredArgument: return "MISSING_REQUIRED_ARGUMENT";
    case ToolErrorCode::WrongArgumentType:       return "WRONG_ARGUMENT_TYPE";
    case ToolErrorCode::ValueOutOfRange:         return "VALUE_OUT_OF_RANGE";
    case ToolErrorCode::ValueNotAllowed:         return "VALUE_NOT_ALLOWED";
    case ToolErrorCode::PermissionDenied:        return "PERMISSION_DENIED";
    case ToolErrorCode::ConfirmationRequired:    return "CONFIRMATION_REQUIRED";
    case ToolErrorCode::ConfirmationInvalid:     return "CONFIRMATION_INVALID";
    case ToolErrorCode::Timeout:                 return "TIMEOUT";
    case ToolErrorCode::Cancelled:               return "CANCELLED";
    case ToolErrorCode::Unavailable:             return "UNAVAILABLE";
    case ToolErrorCode::ExecutionFailed:         return "EXECUTION_FAILED";
    }
    return "EXECUTION_FAILED";
}

const ArgumentSpec* ToolDefinition::findArgument(std::string_view argumentName) const {
    const auto it = std::ranges::find_if(
        arguments,
        [argumentName](const ArgumentSpec& spec) { return spec.name == argumentName; });
    return it != arguments.end() ? &*it : nullptr;
}

std::int64_t ValidatedCall::integerArgument(std::string_view name,
                                            std::int64_t fallback) const {
    const auto it = m_arguments.find(std::string{name});
    if (it == m_arguments.end() || it->second.type != ArgumentType::Integer) {
        return fallback;
    }
    return it->second.integer;
}

std::string ValidatedCall::enumerationArgument(std::string_view name) const {
    const auto it = m_arguments.find(std::string{name});
    if (it == m_arguments.end() || it->second.type != ArgumentType::Enumeration) {
        return {};
    }
    return it->second.enumeration;
}
std::string ValidatedCall::textArgument(std::string_view name) const {
    const auto it = m_arguments.find(std::string{name});
    return it != m_arguments.end() && it->second.type == ArgumentType::Text ? it->second.enumeration : std::string{};
}

bool ValidatedCall::booleanArgument(std::string_view name, bool fallback) const {
    const auto it = m_arguments.find(std::string{name});
    if (it == m_arguments.end() || it->second.type != ArgumentType::Boolean) {
        return fallback;
    }
    return it->second.boolean;
}

ToolResult ToolResult::success(std::string toolName,
                               std::map<std::string, std::string> data) {
    ToolResult result;
    result.m_success = true;
    result.m_toolName = std::move(toolName);
    result.m_data = std::move(data);
    return result;
}

ToolResult ToolResult::failure(std::string toolName, ToolErrorCode code,
                               std::string message) {
    ToolResult result;
    result.m_success = false;
    result.m_toolName = std::move(toolName);
    result.m_errorCode = code;
    result.m_errorMessage = std::move(message);
    return result;
}

std::string ToolResult::toModelText() const {
    // Deliberately flat and machine-shaped. The model's job is to read these
    // values out, not to interpret prose - and a failure must be obviously a
    // failure so it cannot be reported as a measurement.
    if (!m_success) {
        return std::format("TOOL RESULT\ntool: {}\nstatus: FAILED\nerror: {}\ndetail: {}\n",
                           m_toolName, toolErrorName(m_errorCode), m_errorMessage);
    }

    std::string text = std::format("TOOL RESULT\ntool: {}\nstatus: OK\n", m_toolName);
    for (const auto& [key, value] : m_data) {
        text += std::format("{}: {}\n", key, value);
    }
    return text;
}

} // namespace jarvis::tools
