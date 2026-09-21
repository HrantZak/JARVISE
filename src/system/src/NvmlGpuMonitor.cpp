#include "jarvis/system/NvmlGpuMonitor.h"

#include <windows.h>

#include <array>
#include <cstring>
#include <memory>
#include <utility>

namespace jarvis::system {
namespace {

// --- Minimal NVML ABI ------------------------------------------------------
//
// Declared here rather than pulled from the CUDA Toolkit headers: JARVIS uses
// NVML only through this handful of calls, and requiring a 3 GB toolkit install
// to read a GPU temperature would be absurd. These signatures are part of
// NVML's stable, versioned C ABI.

using NvmlReturn = int;
constexpr NvmlReturn kNvmlSuccess = 0;

using NvmlDevice = void*;

struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};

struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

constexpr int kTemperatureSensorGpu = 0;
constexpr unsigned kNameBufferSize = 96;
constexpr unsigned kDriverBufferSize = 96;

std::string trimmed(const std::array<char, kNameBufferSize>& buffer) {
    const std::size_t length = ::strnlen(buffer.data(), buffer.size());
    return std::string{buffer.data(), length};
}

} // namespace

struct NvmlGpuMonitor::Api {
    NvmlReturn(*init)();
    NvmlReturn(*shutdown)();
    NvmlReturn(*deviceGetCount)(unsigned int*);
    NvmlReturn(*deviceGetHandleByIndex)(unsigned int, NvmlDevice*);
    NvmlReturn(*deviceGetName)(NvmlDevice, char*, unsigned int);
    NvmlReturn(*deviceGetUtilizationRates)(NvmlDevice, NvmlUtilization*);
    NvmlReturn(*deviceGetMemoryInfo)(NvmlDevice, NvmlMemory*);
    NvmlReturn(*deviceGetTemperature)(NvmlDevice, int, unsigned int*);
    NvmlReturn(*systemGetDriverVersion)(char*, unsigned int);
};

NvmlGpuMonitor::NvmlGpuMonitor() {
    load();
}

NvmlGpuMonitor::~NvmlGpuMonitor() {
    unload();
}

void NvmlGpuMonitor::load() {
    HMODULE module = ::LoadLibraryW(L"nvml.dll");
    if (module == nullptr) {
        m_unavailableReason = "nvml.dll not found (no NVIDIA driver on this machine)";
        return;
    }
    m_library = module;

    auto resolve = [module](const char* name) -> void* {
        return reinterpret_cast<void*>(::GetProcAddress(module, name));
    };

    auto api = std::make_unique<Api>();
    api->init = reinterpret_cast<decltype(Api::init)>(resolve("nvmlInit_v2"));
    api->shutdown = reinterpret_cast<decltype(Api::shutdown)>(resolve("nvmlShutdown"));
    api->deviceGetCount =
        reinterpret_cast<decltype(Api::deviceGetCount)>(resolve("nvmlDeviceGetCount_v2"));
    api->deviceGetHandleByIndex = reinterpret_cast<decltype(Api::deviceGetHandleByIndex)>(
        resolve("nvmlDeviceGetHandleByIndex_v2"));
    api->deviceGetName =
        reinterpret_cast<decltype(Api::deviceGetName)>(resolve("nvmlDeviceGetName"));
    api->deviceGetUtilizationRates = reinterpret_cast<decltype(Api::deviceGetUtilizationRates)>(
        resolve("nvmlDeviceGetUtilizationRates"));
    api->deviceGetMemoryInfo = reinterpret_cast<decltype(Api::deviceGetMemoryInfo)>(
        resolve("nvmlDeviceGetMemoryInfo"));
    api->deviceGetTemperature = reinterpret_cast<decltype(Api::deviceGetTemperature)>(
        resolve("nvmlDeviceGetTemperature"));
    api->systemGetDriverVersion = reinterpret_cast<decltype(Api::systemGetDriverVersion)>(
        resolve("nvmlSystemGetDriverVersion"));

    if (api->init == nullptr || api->deviceGetCount == nullptr ||
        api->deviceGetHandleByIndex == nullptr || api->deviceGetUtilizationRates == nullptr ||
        api->deviceGetMemoryInfo == nullptr) {
        m_unavailableReason = "nvml.dll is missing the entry points JARVIS needs";
        unload();
        return;
    }

    if (api->init() != kNvmlSuccess) {
        m_unavailableReason = "nvmlInit failed";
        // shutdown() must not be called after a failed init.
        api->shutdown = nullptr;
        m_api = api.release();
        unload();
        return;
    }

    m_api = api.release();

    unsigned int deviceCount = 0;
    if (m_api->deviceGetCount(&deviceCount) != kNvmlSuccess || deviceCount == 0) {
        m_unavailableReason = "NVML reported no devices";
        unload();
        return;
    }

    // JARVIS targets the primary adapter; multi-GPU selection belongs to the
    // model manager in Phase 11, not to the HUD.
    NvmlDevice device = nullptr;
    if (m_api->deviceGetHandleByIndex(0, &device) != kNvmlSuccess || device == nullptr) {
        m_unavailableReason = "NVML could not open device 0";
        unload();
        return;
    }
    m_device = device;

    if (m_api->deviceGetName != nullptr) {
        std::array<char, kNameBufferSize> buffer{};
        if (m_api->deviceGetName(device, buffer.data(), kNameBufferSize) == kNvmlSuccess) {
            m_deviceName = trimmed(buffer);
        }
    }

    if (m_api->systemGetDriverVersion != nullptr) {
        std::array<char, kNameBufferSize> buffer{};
        if (m_api->systemGetDriverVersion(buffer.data(), kDriverBufferSize) == kNvmlSuccess) {
            m_driverVersion = trimmed(buffer);
        }
    }

    NvmlMemory memory{};
    if (m_api->deviceGetMemoryInfo(device, &memory) == kNvmlSuccess) {
        m_vramTotalBytes = memory.total;
    }

    m_available = true;
}

void NvmlGpuMonitor::unload() noexcept {
    if (m_api != nullptr) {
        if (m_api->shutdown != nullptr) {
            m_api->shutdown();
        }
        delete m_api;
        m_api = nullptr;
    }
    if (m_library != nullptr) {
        ::FreeLibrary(static_cast<HMODULE>(m_library));
        m_library = nullptr;
    }
    m_device = nullptr;
    m_available = false;
}

GpuMetrics NvmlGpuMonitor::sample() {
    GpuMetrics metrics;
    if (!m_available || m_api == nullptr || m_device == nullptr) {
        return metrics;
    }

    NvmlUtilization utilization{};
    if (m_api->deviceGetUtilizationRates(m_device, &utilization) == kNvmlSuccess) {
        metrics.utilizationPercent = static_cast<double>(utilization.gpu);
        metrics.available = true;
    }

    NvmlMemory memory{};
    if (m_api->deviceGetMemoryInfo(m_device, &memory) == kNvmlSuccess && memory.total > 0) {
        metrics.vramTotalBytes = memory.total;
        metrics.vramUsedBytes = memory.used;
        metrics.vramPercent =
            100.0 * static_cast<double>(memory.used) / static_cast<double>(memory.total);
        metrics.available = true;
    }

    if (m_api->deviceGetTemperature != nullptr) {
        unsigned int celsius = 0;
        if (m_api->deviceGetTemperature(m_device, kTemperatureSensorGpu, &celsius) ==
            kNvmlSuccess) {
            metrics.temperatureCelsius = static_cast<int>(celsius);
            metrics.temperatureValid = true;
        }
    }

    return metrics;
}

} // namespace jarvis::system
