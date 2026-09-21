#include "jarvis/tools/SystemTools.h"

#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <map>
#include <vector>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <optional>
#include <thread>

#include "jarvis/core/Version.h"

namespace jarvis::tools {
namespace {

std::string formatBytes(std::uint64_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    return std::format("{:.2f} GB", static_cast<double>(bytes) / kGiB);
}

/// Base for the read-only tools: they all share the provider and the shape of
/// "take a sample, format it, or report it unavailable".
class SystemToolBase : public ITool {
public:
    SystemToolBase(system::ISystemMetricsProvider& provider, ToolDefinition definition)
        : m_provider{provider}
        , m_definition{std::move(definition)} {}

    [[nodiscard]] const ToolDefinition& definition() const override {
        return m_definition;
    }

protected:
    system::ISystemMetricsProvider& m_provider;
    ToolDefinition m_definition;

    [[nodiscard]] static ToolDefinition readOnly(std::string name,
                                                 std::string description) {
        ToolDefinition definition;
        definition.name = std::move(name);
        definition.description = std::move(description);
        definition.permission = PermissionLevel::ReadOnly;
        definition.timeoutMs = 5000;
        return definition;
    }
};

// --- system_info ----------------------------------------------------------

class SystemInfoTool final : public SystemToolBase {
public:
    explicit SystemInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("system_info",
                                  "Operating system, processor, memory, graphics "
                                  "card and the JARVIS version.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        const system::HardwareProfile& profile = m_provider.profile();

        std::map<std::string, std::string> data;
        data["os"] = profile.osName + " " + profile.osBuild;
        data["cpu"] = profile.cpuName;
        data["cpu_cores"] = std::format("{} cores, {} threads", profile.physicalCores,
                                        profile.logicalProcessors);
        data["memory_total"] = formatBytes(profile.totalRamBytes);
        data["gpu"] = profile.gpuAvailable ? profile.gpuName : "no NVML-capable GPU";
        if (profile.gpuAvailable) {
            data["vram_total"] = formatBytes(profile.vramTotalBytes);
            data["gpu_driver"] = profile.gpuDriverVersion;
        }
        data["jarvis_version"] = std::string{core::buildInfo().version};

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- cpu_info -------------------------------------------------------------

class CpuInfoTool final : public SystemToolBase {
public:
    explicit CpuInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("cpu_info",
                                  "Processor model, core count and current load.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        const system::HardwareProfile& profile = m_provider.profile();

        // Two samples: CPU load is a delta, and the first call has no interval
        // to measure against. Better a short wait than a fabricated number.
        static_cast<void>(m_provider.sample());
        std::this_thread::sleep_for(std::chrono::milliseconds{120});

        const auto snapshot = m_provider.sample();
        if (!snapshot) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       snapshot.error().toUserString());
        }

        std::map<std::string, std::string> data;
        data["model"] = profile.cpuName;
        data["physical_cores"] = std::to_string(profile.physicalCores);
        data["logical_processors"] = std::to_string(profile.logicalProcessors);

        if (snapshot->cpu.valid) {
            data["usage_percent"] = std::format("{:.1f}", snapshot->cpu.usagePercent);
        } else {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "the processor load could not be read");
        }

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- memory_info ----------------------------------------------------------

class MemoryInfoTool final : public SystemToolBase {
public:
    explicit MemoryInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("memory_info",
                                  "Total, used and available system memory.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        const auto snapshot = m_provider.sample();
        if (!snapshot || !snapshot->memory.valid) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "the memory reading is unavailable");
        }

        const system::MemoryMetrics& memory = snapshot->memory;

        std::map<std::string, std::string> data;
        data["total"] = formatBytes(memory.totalBytes);
        data["used"] = formatBytes(memory.usedBytes);
        data["available"] = formatBytes(memory.totalBytes - memory.usedBytes);
        data["usage_percent"] = std::format("{:.1f}", memory.usagePercent);

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- gpu_info -------------------------------------------------------------

class GpuInfoTool final : public SystemToolBase {
public:
    explicit GpuInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("gpu_info",
                                  "Graphics card, video memory, load and "
                                  "temperature.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        const system::HardwareProfile& profile = m_provider.profile();
        if (!profile.gpuAvailable) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "no NVML-capable GPU is present");
        }

        const auto snapshot = m_provider.sample();
        if (!snapshot || !snapshot->gpu.available) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "the GPU reading is unavailable");
        }

        const system::GpuMetrics& gpu = snapshot->gpu;

        std::map<std::string, std::string> data;
        data["name"] = profile.gpuName;
        data["driver"] = profile.gpuDriverVersion;
        data["vram_total"] = formatBytes(gpu.vramTotalBytes);
        data["vram_used"] = formatBytes(gpu.vramUsedBytes);
        data["usage_percent"] = std::format("{:.1f}", gpu.utilizationPercent);
        if (gpu.temperatureValid) {
            data["temperature_celsius"] = std::to_string(gpu.temperatureCelsius);
        }

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- network_info ---------------------------------------------------------

class NetworkInfoTool final : public SystemToolBase {
public:
    explicit NetworkInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("network_info",
                                  "Current network throughput. Reports rates "
                                  "only - no addresses, names or credentials.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        static_cast<void>(m_provider.sample());
        std::this_thread::sleep_for(std::chrono::milliseconds{120});

        const auto snapshot = m_provider.sample();
        if (!snapshot || !snapshot->network.valid) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "the network reading is unavailable");
        }

        // Throughput only. Interface names, addresses and anything that could
        // identify the machine or its network are deliberately not exposed.
        std::map<std::string, std::string> data;
        data["download_bytes_per_second"] =
            std::to_string(snapshot->network.receivedBytesPerSecond);
        data["upload_bytes_per_second"] =
            std::to_string(snapshot->network.sentBytesPerSecond);

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- disk_info ------------------------------------------------------------

class DiskInfoTool final : public SystemToolBase {
public:
    explicit DiskInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("disk_info",
                                  "Free and total space on the system drive.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        ULARGE_INTEGER freeForCaller{};
        ULARGE_INTEGER total{};
        ULARGE_INTEGER free{};

        if (::GetDiskFreeSpaceExW(L"C:\\", &freeForCaller, &total, &free) == FALSE) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "the disk reading is unavailable");
        }

        std::map<std::string, std::string> data;
        data["drive"] = "C:";
        data["total"] = formatBytes(total.QuadPart);
        data["available"] = formatBytes(freeForCaller.QuadPart);
        data["used"] = formatBytes(total.QuadPart - free.QuadPart);

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- battery_info ---------------------------------------------------------

class BatteryInfoTool final : public SystemToolBase {
public:
    explicit BatteryInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("battery_info",
                                  "Battery charge and whether the machine is on "
                                  "mains power.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        SYSTEM_POWER_STATUS status{};
        if (::GetSystemPowerStatus(&status) == FALSE) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "the power status is unavailable");
        }

        std::map<std::string, std::string> data;

        // 128 means "no system battery" - a desktop. Saying so is the honest
        // answer; inventing a charge level would not be.
        if (status.BatteryFlag == 128) {
            data["battery_present"] = "false";
            data["power_source"] = "mains";
        } else {
            data["battery_present"] = "true";
            data["power_source"] = status.ACLineStatus == 1 ? "mains" : "battery";
            if (status.BatteryLifePercent <= 100) {
                data["charge_percent"] = std::to_string(status.BatteryLifePercent);
            }
        }

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- time_info ------------------------------------------------------------

class TimeInfoTool final : public SystemToolBase {
public:
    explicit TimeInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("time_info", "Current local date and time.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        SYSTEMTIME local{};
        ::GetLocalTime(&local);

        std::map<std::string, std::string> data;
        data["date"] = std::format("{:04}-{:02}-{:02}", local.wYear, local.wMonth,
                                   local.wDay);
        data["time"] = std::format("{:02}:{:02}:{:02}", local.wHour, local.wMinute,
                                   local.wSecond);

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- device_info ----------------------------------------------------------

class DeviceInfoTool final : public SystemToolBase {
public:
    explicit DeviceInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         readOnly("device_info",
                                  "What JARVIS itself is running on: build, "
                                  "compiler and GPU backend.")} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& /*cancelled*/) override {
        const core::BuildInfo& build = core::buildInfo();
        const system::HardwareProfile& profile = m_provider.profile();

        std::map<std::string, std::string> data;
        data["jarvis_version"] = std::string{build.version};
        data["build_type"] = std::string{build.buildType};
        data["compiler"] = std::string{build.compiler};
        data["cxx_standard"] = std::string{build.cxxStandard};
        data["qt_version"] = std::string{build.qtVersion};
        data["gpu_backend"] = profile.gpuAvailable ? "Vulkan" : "CPU only";

        return ToolResult::success(call.toolName(), std::move(data));
    }
};

// --- process_info ---------------------------------------------------------

/// Reads back a process name safely.
///
/// **A process name is untrusted text.** Anyone can start a program called
/// whatever they like, including `status: OK` or a line break followed by a
/// forged field. `ToolResult::toModelText()` renders results as `key: value`
/// lines, so a name containing a newline could invent a field that was never
/// measured. Stripping control characters is not tidiness; it is what keeps a
/// value a value.
std::string sanitiseProcessName(std::wstring_view raw) {
    // Longest name kept. Real executables are far shorter; a longer one is
    // either padding or an attempt to crowd the result.
    constexpr std::size_t kMaxNameChars = 64;

    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, raw.data(),
                                             static_cast<int>(raw.size()), nullptr, 0,
                                             nullptr, nullptr);
    std::string utf8;
    if (needed > 0) {
        utf8.resize(static_cast<std::size_t>(needed));
        ::WideCharToMultiByte(CP_UTF8, 0, raw.data(), static_cast<int>(raw.size()),
                              utf8.data(), needed, nullptr, nullptr);
    }

    std::string clean;
    clean.reserve(std::min(utf8.size(), kMaxNameChars));
    for (const char c : utf8) {
        if (clean.size() >= kMaxNameChars) {
            clean += "…";
            break;
        }
        // Control characters go, including the newline and the colon-adjacent
        // tricks. Everything else, including non-Latin names, is kept: a
        // Russian program name is a legitimate name.
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            clean.push_back(' ');
        } else {
            clean.push_back(c);
        }
    }

    return clean.empty() ? std::string{"(unnamed)"} : clean;
}

class ProcessInfoTool final : public SystemToolBase {
public:
    /// Most processes ever reported, whatever the argument asks for. The result
    /// has to stay well inside the context manager's 4096-character cap, and a
    /// list of two hundred processes answers no question a person asked.
    static constexpr std::int64_t kMaxProcesses = 20;

    explicit ProcessInfoTool(system::ISystemMetricsProvider& provider)
        : SystemToolBase{provider,
                         makeDefinition()} {}

    ToolResult execute(const ValidatedCall& call,
                       const std::atomic<bool>& cancelled) override {
        const std::int64_t limit =
            std::clamp(call.integerArgument("limit", 10), std::int64_t{1}, kMaxProcesses);

        // Toolhelp is a read-only snapshot. There is no handle here that could
        // terminate, suspend or write to anything, and no argument that names a
        // process: the tool reports what is running and nothing else.
        const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "the process list is unavailable");
        }

        struct Entry {
            std::string name;
            std::uint32_t pid{0};
            std::uint64_t workingSetBytes{0};
        };
        std::vector<Entry> entries;

        PROCESSENTRY32W process{};
        process.dwSize = sizeof(process);

        if (::Process32FirstW(snapshot, &process) != FALSE) {
            do {
                if (cancelled.load()) {
                    ::CloseHandle(snapshot);
                    return ToolResult::failure(call.toolName(), ToolErrorCode::Cancelled,
                                               "cancelled while reading the process list");
                }

                Entry entry;
                entry.pid = process.th32ProcessID;
                entry.name = sanitiseProcessName(process.szExeFile);

                // Memory needs a handle, and the system refuses one for
                // protected processes. That is not an error - it is a process
                // this account may not inspect - so the row is kept with the
                // size left unknown rather than dropped or invented.
                const HANDLE handle = ::OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.pid);
                if (handle != nullptr) {
                    PROCESS_MEMORY_COUNTERS counters{};
                    if (::GetProcessMemoryInfo(handle, &counters, sizeof(counters))
                        != FALSE) {
                        entry.workingSetBytes = counters.WorkingSetSize;
                    }
                    ::CloseHandle(handle);
                }

                entries.push_back(std::move(entry));
            } while (::Process32NextW(snapshot, &process) != FALSE);
        }

        ::CloseHandle(snapshot);

        if (entries.empty()) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                       "no processes could be read");
        }

        const std::size_t total = entries.size();

        // Largest first: "what is using the memory" is the question this
        // answers. Ties broken by pid so the same machine gives the same
        // answer twice.
        std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
            if (a.workingSetBytes != b.workingSetBytes) {
                return a.workingSetBytes > b.workingSetBytes;
            }
            return a.pid < b.pid;
        });

        std::map<std::string, std::string> data;
        data["total_processes"] = std::to_string(total);
        data["reported"] = std::to_string(std::min<std::size_t>(
            total, static_cast<std::size_t>(limit)));

        for (std::size_t i = 0; i < entries.size() && i < static_cast<std::size_t>(limit);
             ++i) {
            const Entry& entry = entries[i];
            data[std::format("process_{:02}", i + 1)] =
                entry.workingSetBytes > 0
                    ? std::format("{} (pid {}, {})", entry.name, entry.pid,
                                  formatBytes(entry.workingSetBytes))
                    : std::format("{} (pid {}, size not readable)", entry.name,
                                  entry.pid);
        }

        return ToolResult::success(call.toolName(), std::move(data));
    }

private:
    static ToolDefinition makeDefinition() {
        ToolDefinition definition = readOnly(
            "process_info",
            "Running processes, largest first: name, identifier and memory in "
            "use. Reports only - it cannot start, stop or change a process.");
        definition.timeoutMs = 8000;

        // One integer, bounded. There is deliberately no argument naming a
        // process: nothing here takes a name, a path or a command, so there is
        // nothing for one to be smuggled through.
        ArgumentSpec limit;
        limit.name = "limit";
        limit.type = ArgumentType::Integer;
        limit.description = "How many processes to list.";
        limit.required = false;
        limit.minimum = 1;
        limit.maximum = kMaxProcesses;
        definition.arguments.push_back(std::move(limit));

        return definition;
    }
};

} // namespace

std::vector<std::unique_ptr<ITool>> SystemToolFactory::createAll(
    system::ISystemMetricsProvider& provider) {
    std::vector<std::unique_ptr<ITool>> tools;
    tools.push_back(std::make_unique<SystemInfoTool>(provider));
    tools.push_back(std::make_unique<CpuInfoTool>(provider));
    tools.push_back(std::make_unique<MemoryInfoTool>(provider));
    tools.push_back(std::make_unique<GpuInfoTool>(provider));
    tools.push_back(std::make_unique<NetworkInfoTool>(provider));
    tools.push_back(std::make_unique<DiskInfoTool>(provider));
    tools.push_back(std::make_unique<BatteryInfoTool>(provider));
    tools.push_back(std::make_unique<TimeInfoTool>(provider));
    tools.push_back(std::make_unique<DeviceInfoTool>(provider));
    tools.push_back(std::make_unique<ProcessInfoTool>(provider));
    return tools;
}

// ---------------------------------------------------------------------------
// OpenApplicationTool
// ---------------------------------------------------------------------------

namespace {

struct ApplicationEntry {
    std::string_view value;          ///< What the schema accepts.
    AllowedApplication application;
    const wchar_t* target;           ///< Shell verb or executable name.
};

/// The complete allowlist. Every target is a compile-time literal; none of it
/// can be influenced from outside this file.
constexpr std::array<ApplicationEntry, 4> kAllowedApplications{{
    {"calculator", AllowedApplication::Calculator, L"calc.exe"},
    {"notepad", AllowedApplication::Notepad, L"notepad.exe"},
    {"explorer", AllowedApplication::Explorer, L"explorer.exe"},
    {"settings", AllowedApplication::Settings, L"ms-settings:"},
}};

} // namespace

std::optional<AllowedApplication> OpenApplicationTool::resolve(std::string_view value) {
    const auto it = std::ranges::find_if(
        kAllowedApplications,
        [value](const ApplicationEntry& entry) { return entry.value == value; });
    return it != kAllowedApplications.end() ? std::optional{it->application}
                                            : std::nullopt;
}

OpenApplicationTool::OpenApplicationTool() {
    m_definition.name = "open_application";
    m_definition.description =
        "Opens one of a small set of known applications on this machine.";
    m_definition.permission = PermissionLevel::SafeAction;
    m_definition.timeoutMs = 10000;

    ArgumentSpec application;
    application.name = "application";
    application.type = ArgumentType::Enumeration;
    application.description = "Which application to open.";
    application.required = true;
    for (const ApplicationEntry& entry : kAllowedApplications) {
        application.allowedValues.emplace_back(entry.value);
    }

    m_definition.arguments.push_back(std::move(application));
}

ToolResult OpenApplicationTool::execute(const ValidatedCall& call,
                                        const std::atomic<bool>& cancelled) {
    if (cancelled.load()) {
        return ToolResult::failure(call.toolName(), ToolErrorCode::Cancelled,
                                   "cancelled before starting");
    }

    const std::string requested = call.enumerationArgument("application");
    const std::optional<AllowedApplication> application = resolve(requested);

    // Belt and braces: the validator has already restricted this to the
    // allowlist, and this second lookup means a future schema change cannot
    // widen what actually launches without also touching this table.
    if (!application) {
        return ToolResult::failure(call.toolName(), ToolErrorCode::ValueNotAllowed,
                                   "that application is not on the allowlist");
    }

    const auto it = std::ranges::find_if(
        kAllowedApplications, [&application](const ApplicationEntry& entry) {
            return entry.application == *application;
        });

    // ShellExecuteW with a literal target and no parameters. The model
    // contributes an index into a fixed table, never a command line: there is
    // no argument string for it to influence, so there is nothing to escape.
    const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", it->target, nullptr,
                                             nullptr, SW_SHOWNORMAL);

    // ShellExecute returns a value above 32 on success, for historical reasons.
    const auto code = reinterpret_cast<std::intptr_t>(result);
    if (code <= 32) {
        return ToolResult::failure(
            call.toolName(), ToolErrorCode::ExecutionFailed,
            std::format("Windows refused to open the application (code {})", code));
    }

    std::map<std::string, std::string> data;
    data["application"] = requested;
    data["status"] = "opened";
    return ToolResult::success(call.toolName(), std::move(data));
}

// ---------------------------------------------------------------------------
// CloseApplicationTool
// ---------------------------------------------------------------------------

namespace {

struct ClosableEntry {
    std::string_view value;             ///< What the schema accepts.
    ClosableApplication application;
    const wchar_t* executable;          ///< Matched case-insensitively.
    std::string_view label;             ///< For the result text.
};

/// The complete allowlist, compiled in. Explorer is deliberately absent: it is
/// openable and not closable, because closing it takes the taskbar and the
/// desktop with it.
constexpr std::array<ClosableEntry, 10> kClosableApplications{{
    {"telegram", ClosableApplication::Telegram, L"telegram.exe", "Telegram"},
    {"discord", ClosableApplication::Discord, L"discord.exe", "Discord"},
    {"chrome", ClosableApplication::Chrome, L"chrome.exe", "Google Chrome"},
    {"edge", ClosableApplication::Edge, L"msedge.exe", "Microsoft Edge"},
    {"firefox", ClosableApplication::Firefox, L"firefox.exe", "Firefox"},
    {"notepad", ClosableApplication::Notepad, L"notepad.exe", "Notepad"},
    {"calculator", ClosableApplication::Calculator, L"calculatorapp.exe", "Calculator"},
    {"spotify", ClosableApplication::Spotify, L"spotify.exe", "Spotify"},
    {"steam", ClosableApplication::Steam, L"steam.exe", "Steam"},
    {"vlc", ClosableApplication::VlcPlayer, L"vlc.exe", "VLC"},
}};

/// Case-insensitive comparison of a path's file name against \p name.
bool fileNameIs(std::wstring_view path, std::wstring_view name) {
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring_view leaf =
        slash == std::wstring_view::npos ? path : path.substr(slash + 1);

    if (leaf.size() != name.size()) {
        return false;
    }
    for (std::size_t i = 0; i < leaf.size(); ++i) {
        if (::towlower(leaf[i]) != ::towlower(name[i])) {
            return false;
        }
    }
    return true;
}

/// Case-insensitive search for \p folder, with separators normalised.
///
/// Both separators are folded to a backslash first. Windows hands back
/// backslashes, but a path is a path: a check that decides whether something is
/// protected must not turn on which slash it was written with.
bool containsFolder(std::wstring_view path, std::wstring_view folder) {
    std::wstring normalised;
    normalised.reserve(path.size());
    for (const wchar_t c : path) {
        normalised.push_back(c == L'/' ? L'\\' : static_cast<wchar_t>(::towlower(c)));
    }
    return normalised.find(folder) != std::wstring::npos;
}

/// Full image path of a process, empty when it cannot be read - which is itself
/// a reason to leave the process alone.
std::wstring imagePath(DWORD processId) {
    const HANDLE process =
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return {};
    }

    wchar_t buffer[MAX_PATH]{};
    DWORD size = MAX_PATH;
    const BOOL ok = ::QueryFullProcessImageNameW(process, 0, buffer, &size);
    ::CloseHandle(process);

    return ok ? std::wstring{buffer, size} : std::wstring{};
}

/// Visible top-level windows belonging to \p processId.
struct WindowSearch {
    DWORD processId{};
    std::vector<HWND> windows;
};

BOOL CALLBACK collectWindows(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);

    DWORD owner = 0;
    ::GetWindowThreadProcessId(window, &owner);
    if (owner != search->processId) {
        return TRUE;
    }
    // Visible, top-level, and not a tool window: what a person would call "a
    // window of that application". Hidden message-only windows are skipped, so
    // a background process with no user interface is never asked to close.
    if (::IsWindowVisible(window) == FALSE || ::GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }

    search->windows.push_back(window);
    return TRUE;
}

} // namespace

std::optional<ClosableApplication> CloseApplicationTool::resolve(std::string_view value) {
    const auto it = std::ranges::find_if(
        kClosableApplications,
        [value](const ClosableEntry& entry) { return entry.value == value; });
    return it != kClosableApplications.end() ? std::optional{it->application}
                                             : std::nullopt;
}

bool CloseApplicationTool::isProtectedExecutable(std::wstring_view path) {
    if (path.empty()) {
        // Could not be read, which usually means elevated or a service. Not
        // being able to see what something is, is a reason not to touch it.
        return true;
    }

    // JARVIS itself. Closing the assistant because the assistant was asked to
    // is the one instruction it must not follow.
    if (fileNameIs(path, L"jarvis.exe")) {
        return true;
    }

    // The shell. explorer.exe owns the taskbar, the desktop and the Start menu;
    // closing it leaves a person with a blank screen.
    if (fileNameIs(path, L"explorer.exe")) {
        return true;
    }

    // Anything living in the Windows directory. This covers the shell
    // experience hosts, the input host, the settings surfaces and every service
    // binary, without needing to enumerate them.
    return containsFolder(path, L"\\windows\\");
}

CloseApplicationTool::CloseApplicationTool() {
    m_definition.name = "close_application";
    m_definition.description =
        "Closes one of a small set of known applications on this machine by "
        "asking its windows to close, the same way the X button does. The "
        "application decides how to shut down and may ask about unsaved work. "
        "This never terminates a process and never touches Windows itself.";
    m_definition.permission = PermissionLevel::ConfirmRequired;
    m_definition.timeoutMs = 10000;

    ArgumentSpec application;
    application.name = "application";
    application.type = ArgumentType::Enumeration;
    application.description = "Which application to close.";
    application.required = true;
    for (const ClosableEntry& entry : kClosableApplications) {
        application.allowedValues.emplace_back(entry.value);
    }

    m_definition.arguments.push_back(std::move(application));
}

ToolResult CloseApplicationTool::execute(const ValidatedCall& call,
                                         const std::atomic<bool>& cancelled) {
    if (cancelled.load()) {
        return ToolResult::failure(call.toolName(), ToolErrorCode::Cancelled,
                                   "cancelled before starting");
    }

    const std::string requested = call.enumerationArgument("application");
    const std::optional<ClosableApplication> application = resolve(requested);

    // The validator has already restricted this to the allowlist. Looking it up
    // again means a future schema change cannot widen what closes without also
    // touching this table.
    if (!application) {
        return ToolResult::failure(call.toolName(), ToolErrorCode::ValueNotAllowed,
                                   "that application is not on the allowlist");
    }

    const auto entry = std::ranges::find_if(
        kClosableApplications, [&application](const ClosableEntry& candidate) {
            return candidate.application == *application;
        });

    // --- find the running processes ----------------------------------------
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return ToolResult::failure(call.toolName(), ToolErrorCode::ExecutionFailed,
                                   "could not list the running processes");
    }

    std::vector<DWORD> matches;
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);

    if (::Process32FirstW(snapshot, &process) == TRUE) {
        do {
            if (fileNameIs(process.szExeFile, entry->executable)) {
                matches.push_back(process.th32ProcessID);
            }
        } while (::Process32NextW(snapshot, &process) == TRUE);
    }
    ::CloseHandle(snapshot);

    if (matches.empty()) {
        return ToolResult::failure(
            call.toolName(), ToolErrorCode::Unavailable,
            std::format("{} does not appear to be running", entry->label));
    }

    // --- ask the windows to close ------------------------------------------
    const DWORD self = ::GetCurrentProcessId();

    int windowsAsked = 0;
    int processesProtected = 0;
    int processesWithoutWindows = 0;

    for (const DWORD processId : matches) {
        if (cancelled.load()) {
            return ToolResult::failure(call.toolName(), ToolErrorCode::Cancelled,
                                       "cancelled part-way through");
        }

        // The second gate, independent of the allowlist that got us here.
        if (processId == self || isProtectedExecutable(imagePath(processId))) {
            ++processesProtected;
            continue;
        }

        WindowSearch search;
        search.processId = processId;
        ::EnumWindows(collectWindows, reinterpret_cast<LPARAM>(&search));

        if (search.windows.empty()) {
            // A helper process of the same name with no user interface - a
            // browser's renderer, for instance. Nothing to close politely, and
            // killing it is not on offer.
            ++processesWithoutWindows;
            continue;
        }

        for (const HWND window : search.windows) {
            // WM_CLOSE, posted rather than sent: the application handles it on
            // its own thread, at its own pace, and may put up a dialog. This
            // call does not wait and does not force anything.
            if (::PostMessageW(window, WM_CLOSE, 0, 0) != FALSE) {
                ++windowsAsked;
            }
        }
    }

    std::map<std::string, std::string> data;
    data["application"] = std::string{entry->label};
    data["processes_found"] = std::to_string(matches.size());
    data["windows_asked_to_close"] = std::to_string(windowsAsked);

    if (windowsAsked == 0) {
        if (processesProtected > 0) {
            return ToolResult::failure(
                call.toolName(), ToolErrorCode::PermissionDenied,
                std::format("{} is protected and will not be closed", entry->label));
        }
        return ToolResult::failure(
            call.toolName(), ToolErrorCode::Unavailable,
            std::format("{} is running but has no window to close ({} background "
                        "process(es))",
                        entry->label, processesWithoutWindows));
    }

    // Honest about what happened: the request was delivered. Whether the
    // application actually goes away is its decision - it may be asking the
    // user about unsaved work right now - and claiming otherwise would be
    // reporting an outcome nobody observed.
    data["status"] = "asked to close";
    return ToolResult::success(call.toolName(), std::move(data));
}

} // namespace jarvis::tools
