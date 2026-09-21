#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace jarvis::tools {

/// What a tool is allowed to do, decided in C++ and never by the model.
///
/// The level is a property of the tool, fixed when it is registered. Nothing in
/// a model's output can raise it: a `"permission"` field in a tool call is
/// ignored metadata, not a request.
enum class PermissionLevel {
    /// Reads state and changes nothing. Runs without asking.
    ReadOnly,

    /// Changes something harmless and reversible. Runs without asking.
    SafeAction,

    /// Has a visible effect on the machine. Never runs without a human saying
    /// yes to this specific request.
    ConfirmRequired,

    /// Changes something that cannot be undone.
    ///
    /// Like ConfirmRequired, it always asks - but it can never be turned into
    /// an automatic action by any setting, and it is never retried on its own
    /// after a failure. The distinction from ConfirmRequired is not the asking,
    /// it is what happens around it: an interrupted destructive action must be
    /// re-approved rather than resumed.
    ///
    /// No tool carries this level yet. It exists so that when one does, the
    /// rules are already in place and tested rather than invented at the time.
    Destructive,

    /// Never executes, under any circumstances. Present so a tool can be
    /// registered and refused rather than silently missing.
    Denied,
};

[[nodiscard]] std::string_view permissionName(PermissionLevel level) noexcept;

/// The type of a single argument. Deliberately small: every type here can be
/// checked exactly, and anything that cannot be checked exactly has no
/// representation.
enum class ArgumentType {
    Integer,
    Boolean,
    /// One of a fixed set of strings; existing application tools remain closed.
    Enumeration,
    Text, // Bounded text; only tools explicitly declaring it can receive it.
};

/// One argument in a tool's schema.
struct ArgumentSpec {
    std::string name;
    ArgumentType type{ArgumentType::Integer};
    std::string description;
    bool required{false};
    int maxTextChars{4096};

    /// Allowed values for Enumeration. Empty for other types.
    std::vector<std::string> allowedValues;

    /// Inclusive bounds for Integer.
    std::int64_t minimum{0};
    std::int64_t maximum{0};
};

/// What a tool is and what it accepts.
struct ToolDefinition {
    std::string name;
    std::string description;
    PermissionLevel permission{PermissionLevel::Denied};
    std::vector<ArgumentSpec> arguments;

    /// How long execute() may run before it is abandoned.
    int timeoutMs{5000};

    [[nodiscard]] const ArgumentSpec* findArgument(std::string_view name) const;
};

/// A validated argument value. Only produced by the validator - a tool never
/// sees a raw string from the model.
struct ArgumentValue {
    ArgumentType type{ArgumentType::Integer};
    std::int64_t integer{0};
    bool boolean{false};
    std::string enumeration;
};

/// A tool call that has passed validation.
///
/// There is no constructor that takes unvalidated input: the only way to obtain
/// one is through ToolValidator, which is what makes "unvalidated call" an
/// unrepresentable state rather than a rule someone has to remember.
class ValidatedCall {
public:
    [[nodiscard]] const std::string& toolName() const noexcept { return m_toolName; }
    [[nodiscard]] const std::map<std::string, ArgumentValue>& arguments() const noexcept {
        return m_arguments;
    }

    [[nodiscard]] std::int64_t integerArgument(std::string_view name,
                                               std::int64_t fallback = 0) const;
    [[nodiscard]] std::string enumerationArgument(std::string_view name) const;
    [[nodiscard]] std::string textArgument(std::string_view name) const;
    [[nodiscard]] bool booleanArgument(std::string_view name,
                                       bool fallback = false) const;

private:
    friend class ToolValidator;
    ValidatedCall() = default;

    std::string m_toolName;
    std::map<std::string, ArgumentValue> m_arguments;
};

/// Why a tool call failed. Distinct codes because the audit log and the tests
/// both need to distinguish "you asked for something that does not exist" from
/// "you are not allowed to do that".
enum class ToolErrorCode {
    None,
    MalformedJson,
    NotAnObject,
    MissingToolField,
    UnknownTool,
    MissingArguments,
    ArgumentsNotAnObject,
    UnknownArgument,
    MissingRequiredArgument,
    WrongArgumentType,
    ValueOutOfRange,
    ValueNotAllowed,
    PermissionDenied,
    ConfirmationRequired,
    ConfirmationInvalid,
    Timeout,
    Cancelled,
    Unavailable,
    ExecutionFailed,
};

[[nodiscard]] std::string_view toolErrorName(ToolErrorCode code) noexcept;

/// The outcome of a tool call.
///
/// Immutable once constructed, and constructed only in C++. A model can ask for
/// a tool; it cannot manufacture the answer. The result is inserted into the
/// conversation directly rather than generated, so its provenance is not
/// something the model can forge.
class ToolResult {
public:
    static ToolResult success(std::string toolName,
                              std::map<std::string, std::string> data);
    static ToolResult failure(std::string toolName, ToolErrorCode code,
                              std::string message);

    [[nodiscard]] bool ok() const noexcept { return m_success; }
    [[nodiscard]] const std::string& toolName() const noexcept { return m_toolName; }
    [[nodiscard]] const std::map<std::string, std::string>& data() const noexcept {
        return m_data;
    }
    [[nodiscard]] ToolErrorCode errorCode() const noexcept { return m_errorCode; }
    [[nodiscard]] const std::string& errorMessage() const noexcept {
        return m_errorMessage;
    }
    [[nodiscard]] std::chrono::system_clock::time_point timestamp() const noexcept {
        return m_timestamp;
    }

    /// Renders the result as the text handed back to the model. Machine-shaped
    /// and unambiguous, so the model reports rather than invents.
    [[nodiscard]] std::string toModelText() const;

private:
    ToolResult() = default;

    bool m_success{false};
    std::string m_toolName;
    std::map<std::string, std::string> m_data;
    ToolErrorCode m_errorCode{ToolErrorCode::None};
    std::string m_errorMessage;
    std::chrono::system_clock::time_point m_timestamp{std::chrono::system_clock::now()};
};

} // namespace jarvis::tools
