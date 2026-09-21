#include "jarvis/llm/GenerationParams.h"

namespace jarvis::llm {

std::string_view roleName(ChatMessage::Role role) noexcept {
    // These strings go straight into the model's chat template, so they are the
    // conventional lowercase names and are never localised.
    switch (role) {
    case ChatMessage::Role::System:    return "system";
    case ChatMessage::Role::User:      return "user";
    case ChatMessage::Role::Assistant: return "assistant";
    }
    return "user";
}

} // namespace jarvis::llm
