#include "jarvis/tools/ToolValidator.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <format>

namespace jarvis::tools {
namespace {

using Failure = ToolValidator::Failure;

Failure fail(ToolErrorCode code, std::string message) {
    return Failure{code, std::move(message)};
}

/// True when the text is a well-formed UTF-8 sequence. Qt's fromUtf8 replaces
/// bad bytes rather than failing, so malformed input would otherwise slip
/// through as replacement characters.
bool isValidUtf8(std::string_view text) {
    const QByteArray bytes{text.data(), static_cast<qsizetype>(text.size())};
    auto decoder = QStringDecoder{QStringDecoder::Utf8,
                                  QStringDecoder::Flag::Stateless};
    const QString decoded = decoder(bytes);
    return !decoder.hasError() && !decoded.contains(QChar{0xFFFD});
}

} // namespace

ToolValidator::ToolValidator(const ToolRegistry& registry)
    : m_registry{registry} {}

std::string ToolValidator::extractCallJson(std::string_view modelOutput) {
    // Finds the first balanced {...} block. Deliberately dumb: it is a
    // *locator*, not a parser, and everything it finds still has to survive
    // full validation. Nothing here decides anything.
    int depth = 0;
    std::size_t start = std::string_view::npos;
    bool inString = false;
    bool escaped = false;

    for (std::size_t i = 0; i < modelOutput.size(); ++i) {
        const char ch = modelOutput[i];

        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
        } else if (ch == '{') {
            if (depth == 0) {
                start = i;
            }
            ++depth;
        } else if (ch == '}') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && start != std::string_view::npos) {
                    const std::size_t length = i - start + 1;
                    if (length > kMaxCallLength) {
                        return {};
                    }
                    return std::string{modelOutput.substr(start, length)};
                }
            }
        }
    }

    return {};
}

std::expected<ValidatedCall, Failure> ToolValidator::validate(
    std::string_view json) const {

    if (json.empty()) {
        return std::unexpected(fail(ToolErrorCode::MalformedJson, "empty tool call"));
    }
    if (json.size() > kMaxCallLength) {
        return std::unexpected(fail(
            ToolErrorCode::MalformedJson,
            std::format("tool call is {} bytes, limit is {}", json.size(),
                        kMaxCallLength)));
    }
    if (!isValidUtf8(json)) {
        return std::unexpected(
            fail(ToolErrorCode::MalformedJson, "tool call is not valid UTF-8"));
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray{json.data(), static_cast<qsizetype>(json.size())}, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(fail(
            ToolErrorCode::MalformedJson,
            std::format("invalid JSON at offset {}", parseError.offset)));
    }
    if (!document.isObject()) {
        return std::unexpected(
            fail(ToolErrorCode::NotAnObject, "a tool call must be a JSON object"));
    }

    const QJsonObject root = document.object();

    // --- tool name ------------------------------------------------------
    const QJsonValue toolValue = root.value(QStringLiteral("tool"));
    if (!toolValue.isString()) {
        return std::unexpected(fail(ToolErrorCode::MissingToolField,
                                    "the 'tool' field is missing or not a string"));
    }

    const QString toolName = toolValue.toString();
    if (toolName.isEmpty() || toolName.size() > static_cast<qsizetype>(kMaxStringLength)) {
        return std::unexpected(
            fail(ToolErrorCode::UnknownTool, "the tool name is empty or too long"));
    }

    const std::string name = toolName.toStdString();
    const ITool* tool = m_registry.lookup(name);
    if (tool == nullptr) {
        // Covers "powershell", "cmd", "exec", "../open_application" and every
        // other name that is not a registered tool. There is no fallback path.
        return std::unexpected(fail(
            ToolErrorCode::UnknownTool,
            std::format("'{}' is not a registered tool", name)));
    }

    const ToolDefinition& definition = tool->definition();

    // --- arguments ------------------------------------------------------
    // Any field other than "tool" and "arguments" is rejected outright. This is
    // what stops a call carrying "confirmed": true or "permission": "READ_ONLY"
    // - not because those are checked and ignored, but because their presence
    // makes the whole call invalid.
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QString key = it.key();
        if (key != QStringLiteral("tool") && key != QStringLiteral("arguments")) {
            return std::unexpected(fail(
                ToolErrorCode::UnknownArgument,
                std::format("unexpected field '{}' in the tool call",
                            key.toStdString())));
        }
    }

    QJsonObject arguments;
    if (root.contains(QStringLiteral("arguments"))) {
        const QJsonValue argumentsValue = root.value(QStringLiteral("arguments"));
        if (!argumentsValue.isObject()) {
            return std::unexpected(fail(ToolErrorCode::ArgumentsNotAnObject,
                                        "'arguments' must be an object"));
        }
        arguments = argumentsValue.toObject();
    } else if (!definition.arguments.empty()) {
        const bool anyRequired = std::ranges::any_of(
            definition.arguments, [](const ArgumentSpec& spec) { return spec.required; });
        if (anyRequired) {
            return std::unexpected(fail(ToolErrorCode::MissingArguments,
                                        "'arguments' is missing"));
        }
    }

    ValidatedCall call;
    call.m_toolName = name;

    // Every supplied argument must exist in the schema.
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        const std::string argumentName = it.key().toStdString();
        const ArgumentSpec* spec = definition.findArgument(argumentName);
        if (spec == nullptr) {
            return std::unexpected(fail(
                ToolErrorCode::UnknownArgument,
                std::format("'{}' does not accept an argument named '{}'", name,
                            argumentName)));
        }

        const QJsonValue value = it.value();
        ArgumentValue parsed;
        parsed.type = spec->type;

        switch (spec->type) {
        case ArgumentType::Text: {
            if (!value.isString()) return std::unexpected(fail(ToolErrorCode::WrongArgumentType, "text argument must be a string"));
            const QString text = value.toString();
            if (text.isEmpty() || text.size() > spec->maxTextChars || text.contains(QChar(0)))
                return std::unexpected(fail(ToolErrorCode::ValueOutOfRange, "text is empty, exceeds this argument's limit or contains NUL"));
            parsed.enumeration = text.toStdString();
            break;
        }
        case ArgumentType::Integer: {
            // isDouble covers every JSON number; a string "10" is rejected.
            if (!value.isDouble()) {
                return std::unexpected(fail(
                    ToolErrorCode::WrongArgumentType,
                    std::format("'{}' must be an integer", argumentName)));
            }
            const double raw = value.toDouble();
            if (raw != static_cast<double>(static_cast<std::int64_t>(raw))) {
                return std::unexpected(fail(
                    ToolErrorCode::WrongArgumentType,
                    std::format("'{}' must be a whole number", argumentName)));
            }
            const auto integer = static_cast<std::int64_t>(raw);
            if (integer < spec->minimum || integer > spec->maximum) {
                return std::unexpected(fail(
                    ToolErrorCode::ValueOutOfRange,
                    std::format("'{}' must be between {} and {}", argumentName,
                                spec->minimum, spec->maximum)));
            }
            parsed.integer = integer;
            break;
        }

        case ArgumentType::Boolean: {
            if (!value.isBool()) {
                return std::unexpected(fail(
                    ToolErrorCode::WrongArgumentType,
                    std::format("'{}' must be true or false", argumentName)));
            }
            parsed.boolean = value.toBool();
            break;
        }

        case ArgumentType::Enumeration: {
            if (!value.isString()) {
                return std::unexpected(fail(
                    ToolErrorCode::WrongArgumentType,
                    std::format("'{}' must be a string", argumentName)));
            }
            const QString text = value.toString();
            if (text.size() > static_cast<qsizetype>(kMaxStringLength)) {
                return std::unexpected(fail(
                    ToolErrorCode::ValueNotAllowed,
                    std::format("'{}' is too long", argumentName)));
            }

            // Exact membership of the allowed set. This is why
            // "calculator && cmd", "C:\\Windows\\System32\\cmd.exe" and
            // "$(powershell)" are refused: not because they are scanned for
            // dangerous syntax, but because they are not in the list. Nothing
            // here ever interprets the value - it is compared, then handed to
            // the tool as an identifier.
            const std::string candidate = text.toStdString();
            const bool allowed = std::ranges::find(spec->allowedValues, candidate) !=
                                 spec->allowedValues.end();
            if (!allowed) {
                return std::unexpected(fail(
                    ToolErrorCode::ValueNotAllowed,
                    std::format("'{}' is not an accepted value for '{}'", candidate,
                                argumentName)));
            }
            parsed.enumeration = candidate;
            break;
        }
        }

        call.m_arguments.emplace(argumentName, std::move(parsed));
    }

    // Every required argument must be present.
    for (const ArgumentSpec& spec : definition.arguments) {
        if (spec.required && !call.m_arguments.contains(spec.name)) {
            return std::unexpected(fail(
                ToolErrorCode::MissingRequiredArgument,
                std::format("'{}' requires the argument '{}'", name, spec.name)));
        }
    }

    return call;
}

} // namespace jarvis::tools
