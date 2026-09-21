#include "jarvis/system/WindowsSystemMetricsProvider.h"

// Winsock must precede windows.h: WIN32_LEAN_AND_MEAN keeps the old winsock
// out, and netioapi.h needs the v2 types.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace jarvis::system {
namespace {

constexpr double kHundredNanosecondsPerSecond = 1.0e7;

std::uint64_t toUint64(const FILETIME& value) noexcept {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int required = ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                               static_cast<int>(text.size()),
                                               nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(required), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), required, nullptr, nullptr);
    return out;
}

/// Reads a REG_SZ value; returns an empty string when it is absent.
std::string readRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* value) {
    DWORD sizeBytes = 0;
    if (::RegGetValueW(root, subKey, value, RRF_RT_REG_SZ, nullptr, nullptr, &sizeBytes) !=
        ERROR_SUCCESS) {
        return {};
    }

    std::wstring buffer(sizeBytes / sizeof(wchar_t), L'\0');
    if (::RegGetValueW(root, subKey, value, RRF_RT_REG_SZ, nullptr, buffer.data(), &sizeBytes) !=
        ERROR_SUCCESS) {
        return {};
    }

    // RegGetValue reports the size including the terminating null.
    while (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return toUtf8(buffer);
}

unsigned countPhysicalCores() {
    DWORD lengthBytes = 0;
    ::GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &lengthBytes);
    if (lengthBytes == 0 || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return 0;
    }

    std::vector<std::byte> buffer(lengthBytes);
    auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
    if (::GetLogicalProcessorInformationEx(RelationProcessorCore, first, &lengthBytes) == FALSE) {
        return 0;
    }

    unsigned cores = 0;
    DWORD offset = 0;
    while (offset < lengthBytes) {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
            buffer.data() + offset);
        if (entry->Size == 0) {
            break;
        }
        if (entry->Relationship == RelationProcessorCore) {
            ++cores;
        }
        offset += entry->Size;
    }
    return cores;
}

} // namespace

WindowsSystemMetricsProvider::WindowsSystemMetricsProvider()
    : m_gpu{std::make_unique<NvmlGpuMonitor>()} {
    readHardwareProfile();
}

WindowsSystemMetricsProvider::~WindowsSystemMetricsProvider() = default;

const std::string& WindowsSystemMetricsProvider::gpuUnavailableReason() const noexcept {
    return m_gpu->unavailableReason();
}

void WindowsSystemMetricsProvider::readHardwareProfile() {
    m_profile.cpuName = readRegistryString(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString");

    // Trim the padding Intel/AMD leave in that registry value.
    while (!m_profile.cpuName.empty() && m_profile.cpuName.back() == ' ') {
        m_profile.cpuName.pop_back();
    }

    SYSTEM_INFO info{};
    ::GetNativeSystemInfo(&info);
    m_profile.logicalProcessors = info.dwNumberOfProcessors;
    m_profile.physicalCores = countPhysicalCores();

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (::GlobalMemoryStatusEx(&memory) != FALSE) {
        m_profile.totalRamBytes = memory.ullTotalPhys;
    }

    constexpr const wchar_t* kCurrentVersion =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    m_profile.osName = readRegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"ProductName");

    const std::string displayVersion =
        readRegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"DisplayVersion");
    const std::string build =
        readRegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"CurrentBuildNumber");

    if (!displayVersion.empty() && !build.empty()) {
        m_profile.osBuild = std::format("{} (build {})", displayVersion, build);
    } else {
        m_profile.osBuild = build;
    }

    // ProductName still reads "Windows 10 ..." on Windows 11: Microsoft never
    // updated the value, and the build number is the only reliable signal.
    // 22000 is the first Windows 11 build. Left uncorrected, system_info would
    // tell the user - and the model - the wrong operating system.
    if (const int buildNumber = build.empty() ? 0 : std::atoi(build.c_str());
        buildNumber >= 22000 && m_profile.osName.starts_with("Windows 10")) {
        m_profile.osName.replace(0, std::string_view{"Windows 10"}.size(), "Windows 11");
    }

    m_profile.gpuAvailable = m_gpu->isAvailable();
    m_profile.gpuName = m_gpu->deviceName();
    m_profile.gpuDriverVersion = m_gpu->driverVersion();
    m_profile.vramTotalBytes = m_gpu->vramTotalBytes();
}

CpuMetrics WindowsSystemMetricsProvider::sampleCpu() {
    CpuMetrics metrics;

    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (::GetSystemTimes(&idleTime, &kernelTime, &userTime) == FALSE) {
        return metrics;
    }

    const std::uint64_t idle = toUint64(idleTime);
    // Kernel time already includes idle time, so busy = kernel + user - idle.
    const std::uint64_t total = toUint64(kernelTime) + toUint64(userTime);
    const std::uint64_t busy = total - idle;

    if (!m_hasCpuBaseline) {
        m_previousIdleTicks = idle;
        m_previousBusyTicks = busy;
        m_hasCpuBaseline = true;
        return metrics;  // no interval yet
    }

    const std::uint64_t idleDelta = idle - m_previousIdleTicks;
    const std::uint64_t busyDelta = busy - m_previousBusyTicks;
    m_previousIdleTicks = idle;
    m_previousBusyTicks = busy;

    const std::uint64_t totalDelta = idleDelta + busyDelta;
    if (totalDelta == 0) {
        return metrics;
    }

    metrics.usagePercent =
        std::clamp(100.0 * static_cast<double>(busyDelta) / static_cast<double>(totalDelta),
                   0.0, 100.0);
    metrics.valid = true;
    return metrics;
}

MemoryMetrics WindowsSystemMetricsProvider::sampleMemory() const {
    MemoryMetrics metrics;

    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (::GlobalMemoryStatusEx(&status) == FALSE || status.ullTotalPhys == 0) {
        return metrics;
    }

    metrics.totalBytes = status.ullTotalPhys;
    metrics.usedBytes = status.ullTotalPhys - status.ullAvailPhys;
    metrics.usagePercent = std::clamp(100.0 * static_cast<double>(metrics.usedBytes) /
                                          static_cast<double>(metrics.totalBytes),
                                      0.0, 100.0);
    metrics.valid = true;
    return metrics;
}

NetworkMetrics WindowsSystemMetricsProvider::sampleNetwork() {
    NetworkMetrics metrics;

    MIB_IF_TABLE2* table = nullptr;
    if (::GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return metrics;
    }

    std::uint64_t received = 0;
    std::uint64_t sent = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        if (row.OperStatus != IfOperStatusUp) {
            continue;
        }
        received += row.InOctets;
        sent += row.OutOctets;
    }
    ::FreeMibTable(table);

    const auto now = std::chrono::steady_clock::now();

    if (!m_hasNetworkBaseline) {
        m_previousReceivedBytes = received;
        m_previousSentBytes = sent;
        m_previousNetworkAt = now;
        m_hasNetworkBaseline = true;
        return metrics;
    }

    const double seconds =
        std::chrono::duration<double>(now - m_previousNetworkAt).count();

    // Counters reset when an adapter is disabled or reconnected; treat a
    // backwards jump as a new baseline rather than reporting a huge rate.
    const bool countersWentBackwards =
        received < m_previousReceivedBytes || sent < m_previousSentBytes;

    if (seconds > 0.0 && !countersWentBackwards) {
        metrics.receivedBytesPerSecond = static_cast<std::uint64_t>(
            static_cast<double>(received - m_previousReceivedBytes) / seconds);
        metrics.sentBytesPerSecond = static_cast<std::uint64_t>(
            static_cast<double>(sent - m_previousSentBytes) / seconds);
        metrics.valid = true;
    }

    m_previousReceivedBytes = received;
    m_previousSentBytes = sent;
    m_previousNetworkAt = now;
    return metrics;
}

core::Result<SystemSnapshot> WindowsSystemMetricsProvider::sample() {
    SystemSnapshot snapshot;
    snapshot.cpu = sampleCpu();
    snapshot.memory = sampleMemory();
    snapshot.network = sampleNetwork();
    snapshot.gpu = m_gpu->sample();

    // Memory is the one reading that should never fail on a healthy system; if
    // even that is gone, something is badly wrong and the caller should know.
    if (!snapshot.memory.valid) {
        return core::fail(core::ErrorCode::Unavailable,
                          "GlobalMemoryStatusEx failed; no system metrics are readable");
    }

    return snapshot;
}

} // namespace jarvis::system
