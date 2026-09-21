#include "jarvis/tools/ToolRegistry.h"

#include <algorithm>
#include <format>
#include <utility>

namespace jarvis::tools {

bool ToolRegistry::add(std::unique_ptr<ITool> tool) {
    if (!tool) {
        return false;
    }

    const std::string& name = tool->definition().name;
    if (name.empty() || contains(name)) {
        return false;
    }

    m_tools.push_back(std::move(tool));
    return true;
}

ITool* ToolRegistry::lookup(std::string_view name) const {
    // Exact match only. No trimming, no case folding, no separator handling:
    // "../open_application", " open_application" and "OPEN_APPLICATION" are all
    // simply different strings that are not registered.
    const auto it = std::ranges::find_if(m_tools, [name](const auto& tool) {
        return tool->definition().name == name;
    });
    return it != m_tools.end() ? it->get() : nullptr;
}

std::vector<const ToolDefinition*> ToolRegistry::definitions() const {
    std::vector<const ToolDefinition*> result;
    result.reserve(m_tools.size());
    for (const auto& tool : m_tools) {
        result.push_back(&tool->definition());
    }
    return result;
}

std::string ToolRegistry::describeForModel() const {
    // The calling convention, then the catalogue.
    //
    // Every sentence here is a hint that makes the model *more likely* to
    // produce something usable. Not one of them is load-bearing for safety: a
    // model that ignores all of it, invents a tool, adds fields or supplies a
    // path gets exactly the same refusal as one that never read it. The rules
    // that matter are in ToolValidator and PermissionManager, in C++.
    std::string text =
        "TOOLS\n"
        "You can look things up on this machine by emitting a single JSON "
        "object, on its own, with no other text:\n"
        "{\"tool\": \"<name>\", \"arguments\": {}}\n"
        "Only the fields \"tool\" and \"arguments\" are allowed. Use a tool when "
        "the user asks about this computer rather than guessing. You will "
        "receive the result and should then answer in plain language.\n"
        "Available tools:\n";

    for (const auto& tool : m_tools) {
        const ToolDefinition& definition = tool->definition();
        if (definition.permission == PermissionLevel::Denied) {
            // A denied tool is not offered. It exists so the name is taken and
            // the refusal is explicit, not so a model can try it.
            continue;
        }

        text += std::format("- {}: {}", definition.name, definition.description);

        if (definition.permission == PermissionLevel::ConfirmRequired) {
            text += " [needs the user's confirmation]";
        }
        text += "\n";

        for (const ArgumentSpec& argument : definition.arguments) {
            text += std::format("    {}{}: ", argument.name,
                                argument.required ? "" : " (optional)");

            switch (argument.type) {
            case ArgumentType::Text:
                text += std::format("text, 1..{} characters", argument.maxTextChars);
                break;
            case ArgumentType::Integer:
                text += std::format("integer {}..{}", argument.minimum, argument.maximum);
                break;
            case ArgumentType::Boolean:
                text += "true or false";
                break;
            case ArgumentType::Enumeration: {
                text += "one of ";
                for (std::size_t i = 0; i < argument.allowedValues.size(); ++i) {
                    text += std::format("{}{}", i == 0 ? "" : ", ",
                                        argument.allowedValues[i]);
                }
                break;
            }
            }

            if (!argument.description.empty()) {
                text += std::format(" - {}", argument.description);
            }
            text += "\n";
        }
    }

    return text;
}

} // namespace jarvis::tools
