#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include "jarvis/system/ISystemMetricsProvider.h"
#include "jarvis/system/NvmlGpuMonitor.h"

namespace jarvis::system {

/// Live telemetry from native Windows APIs.
///
///   CPU      GetSystemTimes, differenced between samples
///   Memory   GlobalMemoryStatusEx
///   Network  GetIfTable2, summed over operational non-loopback interfaces
///   GPU      NVML (see NvmlGpuMonitor)
///
/// No WMI: it is slow, can block for seconds, and would need COM initialised on
/// the sampling thread. Everything here is a direct kernel call.
class WindowsSystemMetricsProvider final : public ISystemMetricsProvider {
public:
    WindowsSystemMetricsProvider();
    ~WindowsSystemMetricsProvider() override;

    [[nodiscard]] core::Result<SystemSnapshot> sample() override;
    [[nodiscard]] const HardwareProfile& profile() const noexcept override { return m_profile; }

    /// Empty when the GPU is available; otherwise explains why it is not.
    [[nodiscard]] const std::string& gpuUnavailableReason() const noexcept;

private:
    void readHardwareProfile();
    [[nodiscard]] CpuMetrics sampleCpu();
    [[nodiscard]] MemoryMetrics sampleMemory() const;
    [[nodiscard]] NetworkMetrics sampleNetwork();

    HardwareProfile m_profile;
    std::unique_ptr<NvmlGpuMonitor> m_gpu;

    // CPU delta state
    std::uint64_t m_previousIdleTicks{0};
    std::uint64_t m_previousBusyTicks{0};
    bool m_hasCpuBaseline{false};

    // Network delta state
    std::uint64_t m_previousReceivedBytes{0};
    std::uint64_t m_previousSentBytes{0};
    std::chrono::steady_clock::time_point m_previousNetworkAt{};
    bool m_hasNetworkBaseline{false};
};

} // namespace jarvis::system
