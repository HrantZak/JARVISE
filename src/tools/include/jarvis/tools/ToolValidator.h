#pragma once

#include <string>
#include <string_view>

#include "jarvis/core/Result.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolTypes.h"

namespace jarvis::tools {

/// Turns untrusted text into a ValidatedCall, or refuses.
///
/// This is the security boundary of the whole tool system. It assumes the input
/// is hostile: not merely a confused model, but output crafted to get past it.
/// Everything is rejected unless it is explicitly allowed - unknown tools,
/// unknown arguments, wrong types, out-of-range numbers, values outside an
/// enumeration.
///
/// **The system prompt is not a security boundary.** A model that ignores every
/// instruction it was given still cannot get anything through here, because
/// nothing in the parser consults the prompt.
class ToolValidator {
public:
    struct Failure {
        ToolErrorCode code{ToolErrorCode::None};
        std::string message;
    };

    explicit ToolValidator(const ToolRegistry& registry);

    /// Extracts the first tool call from model output, if there is one.
    ///
    /// Returns an empty string when the text contains no call at all, which is
    /// the ordinary case for conversational replies.
    [[nodiscard]] static std::string extractCallJson(std::string_view modelOutput);

    /// Parses and validates \p json against the registry.
    [[nodiscard]] std::expected<ValidatedCall, Failure> validate(
        std::string_view json) const;

    /// Longest accepted JSON payload. A model looping on output must not be
    /// able to make the parser allocate without bound.
    static constexpr std::size_t kMaxCallLength = 131072;

    /// Longest accepted string value, well beyond any legitimate enum name.
    static constexpr std::size_t kMaxStringLength = 256;

private:
    const ToolRegistry& m_registry;
};

} // namespace jarvis::tools
