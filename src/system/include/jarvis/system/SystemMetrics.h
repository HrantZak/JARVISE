#pragma once

#include <cstdint>
#include <string>

namespace jarvis::system {

/// Processor load since the previous sample.
struct CpuMetrics {
    bool valid{false};
    double usagePercent{0.0};
};

struct MemoryMetrics {
    bool valid{false};
    std::uint64_t totalBytes{0};
    std::uint64_t usedBytes{0};
    double usagePercent{0.0};
};

/// GPU readings. `available` is false when no NVML-capable adapter answered;
/// in that case every other field stays zero and the UI must show the metric as
/// unavailable rather than as zero load.
struct GpuMetrics {
    bool available{false};
    double utilizationPercent{0.0};
    std::uint64_t vramTotalBytes{0};
    std::uint64_t vramUsedBytes{0};
    double vramPercent{0.0};
    bool temperatureValid{false};
    int temperatureCelsius{0};
};

/// Aggregate throughput over every operational, non-loopback interface.
struct NetworkMetrics {
    bool valid{false};
    std::uint64_t receivedBytesPerSecond{0};
    std::uint64_t sentBytesPerSecond{0};
};

/// One poll of everything the HUD shows.
struct SystemSnapshot {
    CpuMetrics cpu;
    MemoryMetrics memory;
    GpuMetrics gpu;
    NetworkMetrics network;
};

/// Values that do not change while JARVIS runs, read once at start-up.
struct HardwareProfile {
    std::string cpuName;
    unsigned physicalCores{0};
    unsigned logicalProcessors{0};
    std::uint64_t totalRamBytes{0};

    bool gpuAvailable{false};
    std::string gpuName;
    std::string gpuDriverVersion;
    std::uint64_t vramTotalBytes{0};

    std::string osName;
    std::string osBuild;
};

} // namespace jarvis::system
