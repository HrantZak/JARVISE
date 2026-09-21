#pragma once

#include <atomic>

#include "jarvis/tools/ITool.h"

namespace jarvis::tools {

/// Read-only discovery of devices visible on the local IPv4 subnet.
///
/// The implementation uses the Windows IP Helper neighbour table and bounded
/// ICMP echo probes. It never opens a port, authenticates to a device, or
/// changes network state. A generic LAN scan cannot reliably reveal a phone's
/// exact model, so the result reports the hostname/MAC and labels model data
/// as unavailable when the device does not publish it.
class NetworkDiscoveryTool final : public ITool {
public:
    NetworkDiscoveryTool();

    [[nodiscard]] const ToolDefinition& definition() const override {
        return m_definition;
    }

    [[nodiscard]] ToolResult execute(const ValidatedCall& call,
                                     const std::atomic<bool>& cancelled) override;

private:
    ToolDefinition m_definition;
};

} // namespace jarvis::tools
