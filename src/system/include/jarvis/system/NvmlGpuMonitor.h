#pragma once

#include <string>

#include "jarvis/system/SystemMetrics.h"

namespace jarvis::system {

/// GPU telemetry through NVML.
///
/// nvml.dll ships with the NVIDIA display driver, so this needs no CUDA
/// Toolkit and adds no third-party dependency to the build. It is loaded at
/// run time with LoadLibrary: on a machine with no NVIDIA GPU the library
/// simply is not there, isAvailable() stays false and the UI reports the GPU
/// metrics as unavailable instead of inventing zeros.
///
/// Not thread-safe; owned and used by a single provider on one worker thread.
class NvmlGpuMonitor {
public:
    NvmlGpuMonitor();
    ~NvmlGpuMonitor();

    NvmlGpuMonitor(const NvmlGpuMonitor&) = delete;
    NvmlGpuMonitor& operator=(const NvmlGpuMonitor&) = delete;

    [[nodiscard]] bool isAvailable() const noexcept { return m_available; }

    /// Why the GPU is unavailable, for the log. Empty when it is available.
    [[nodiscard]] const std::string& unavailableReason() const noexcept {
        return m_unavailableReason;
    }

    [[nodiscard]] const std::string& deviceName() const noexcept { return m_deviceName; }
    [[nodiscard]] const std::string& driverVersion() const noexcept { return m_driverVersion; }
    [[nodiscard]] std::uint64_t vramTotalBytes() const noexcept { return m_vramTotalBytes; }

    /// Reads utilisation, VRAM and temperature. Returns an all-zero, unavailable
    /// result when NVML is not usable.
    [[nodiscard]] GpuMetrics sample();

private:
    struct Api;

    void load();
    void unload() noexcept;

    void* m_library{nullptr};
    Api* m_api{nullptr};
    void* m_device{nullptr};

    bool m_available{false};
    std::string m_unavailableReason;
    std::string m_deviceName;
    std::string m_driverVersion;
    std::uint64_t m_vramTotalBytes{0};
};

} // namespace jarvis::system
