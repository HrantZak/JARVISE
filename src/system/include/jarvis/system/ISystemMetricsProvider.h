#pragma once

#include "jarvis/core/Result.h"
#include "jarvis/system/SystemMetrics.h"

namespace jarvis::system {

/// Source of live machine telemetry.
///
/// Implementations report per-metric availability instead of failing wholesale:
/// a machine without an NVML GPU must still deliver CPU, memory and network
/// readings. sample() only returns an error when nothing at all could be read.
class ISystemMetricsProvider {
public:
    ISystemMetricsProvider() = default;
    virtual ~ISystemMetricsProvider() = default;

    ISystemMetricsProvider(const ISystemMetricsProvider&) = delete;
    ISystemMetricsProvider& operator=(const ISystemMetricsProvider&) = delete;

    /// Reads current values. Rate-based metrics (CPU, network) are deltas since
    /// the previous call, so the first call after construction reports them as
    /// invalid - there is no interval to divide by yet.
    ///
    /// Called from a worker thread, never from the GUI thread.
    [[nodiscard]] virtual core::Result<SystemSnapshot> sample() = 0;

    /// Static machine description, read once during construction.
    [[nodiscard]] virtual const HardwareProfile& profile() const noexcept = 0;
};

} // namespace jarvis::system
