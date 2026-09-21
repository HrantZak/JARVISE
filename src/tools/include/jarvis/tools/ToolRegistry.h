#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "jarvis/tools/ITool.h"

namespace jarvis::tools {

/// The complete, fixed set of things JARVIS can do.
///
/// **There is no way to add a tool at run time from outside the composition
/// root.** `add()` takes a `std::unique_ptr<ITool>` - a C++ object that has to
/// be compiled in. No method takes a name, a description or a script and turns
/// it into a capability. A model asking to "register a tool" is not refused by
/// a check; the operation simply has no representation in this API.
///
/// Registration happens once, at start-up, before any model is loaded. After
/// that the registry is read-only in practice, and `lookup()` is safe to call
/// from any thread.
class ToolRegistry {
public:
    ToolRegistry() = default;

    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    /// Adds a tool. Called only from the composition root. Returns false if a
    /// tool of the same name is already present - names are identities, and
    /// silently replacing one would let a later registration shadow an earlier
    /// permission level.
    bool add(std::unique_ptr<ITool> tool);

    /// Finds a tool by exact name. No fuzzy matching, no normalisation, no path
    /// resolution: "../open_application" is simply not a registered name.
    [[nodiscard]] ITool* lookup(std::string_view name) const;

    [[nodiscard]] bool contains(std::string_view name) const {
        return lookup(name) != nullptr;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_tools.size(); }

    /// Definitions of every registered tool, for the UI and for the description
    /// handed to the model.
    [[nodiscard]] std::vector<const ToolDefinition*> definitions() const;

    /// The tool list as it is described to the model. This is documentation for
    /// the model, not a permission grant: what actually runs is decided by the
    /// validator and the permission manager, whatever the model was told.
    [[nodiscard]] std::string describeForModel() const;

private:
    std::vector<std::unique_ptr<ITool>> m_tools;
};

} // namespace jarvis::tools
