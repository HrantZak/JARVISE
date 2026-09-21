#include "jarvis/tools/ShellTool.h"
#include <windows.h>
#include <QDir>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QStringDecoder>
#include <algorithm>
#include <vector>
namespace jarvis::tools {
namespace {
struct Handle {
    HANDLE value{nullptr};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    void close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
QString decode(const QByteArray& bytes, bool cmd) {
    if (cmd && bytes.contains('\0')) {
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        return decoder(bytes);
    }
    if (!cmd) return QString::fromUtf8(bytes);
    const int count = MultiByteToWideChar(CP_OEMCP, 0, bytes.constData(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(static_cast<size_t>(count), L'\0');
    if (count) MultiByteToWideChar(CP_OEMCP, 0, bytes.constData(), static_cast<int>(bytes.size()), text.data(), count);
    return QString::fromStdWString(text);
}
}
CommandOutcome runWindowsCommand(const QString& shell, const QString& command,
    const QString& directory, const std::atomic<bool>& cancelled, int timeoutMs) {
    CommandOutcome result;
    if (cancelled.load()) { result.cancelled = true; return result; }
    if ((shell != "cmd" && shell != "powershell") || command.trimmed().isEmpty() || command.size() > 4096 || command.contains(QChar(0)) || !QDir::isAbsolutePath(directory) || !QDir(directory).exists()) {
        result.output = QStringLiteral("Invalid shell, command or working directory."); return result;
    }
    wchar_t system[MAX_PATH];
    const UINT size = GetSystemDirectoryW(system, MAX_PATH);
    if (!size || size >= MAX_PATH) { result.output = "Cannot locate Windows system directory."; return result; }
    const QString executable = QString::fromWCharArray(system) + (shell == "cmd" ? "/cmd.exe" : "/WindowsPowerShell/v1.0/powershell.exe");
    QString arguments;
    if (shell == "cmd") arguments = QStringLiteral(" /D /U /S /C \"") + command + '"';
    else {
        const QString script = QStringLiteral("$OutputEncoding = [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); $ErrorActionPreference='Stop'; $global:LASTEXITCODE=0;\n") + command + QStringLiteral("\nif (-not $?) { exit 1 }; exit $LASTEXITCODE");
        const QByteArray utf16(reinterpret_cast<const char*>(script.utf16()), script.size() * 2);
        arguments = QStringLiteral(" -NoLogo -NoProfile -NonInteractive -OutputFormat Text -EncodedCommand ") + QString::fromLatin1(utf16.toBase64());
    }
    std::wstring line = (QStringLiteral("\"") + QDir::toNativeSeparators(executable) + '"' + arguments).toStdWString();
    const std::wstring exe = QDir::toNativeSeparators(executable).toStdWString();
    const std::wstring cwd = QDir::toNativeSeparators(directory).toStdWString();
    Handle read, write, input, job, process, thread;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&read.value, &write.value, &security, 0) || !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0)) {
        result.output = "Cannot create command output pipe."; return result;
    }
    input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    job.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (input.value == INVALID_HANDLE_VALUE || !job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        result.output = "Cannot prepare a cancellable command process."; return result;
    }
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<unsigned char> storage(attributeSize);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize)) { result.output = "Cannot initialise process attributes."; return result; }
    HANDLE inherited[]{write.value, input.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributes); result.output = "Cannot restrict inherited handles."; return result;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = write.value;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | BELOW_NORMAL_PRIORITY_CLASS,
        nullptr, cwd.c_str(), &startup.StartupInfo, &info);
    DeleteProcThreadAttributeList(attributes);
    if (!created) { result.output = QStringLiteral("Windows could not start the command (%1).").arg(GetLastError()); return result; }
    process.value = info.hProcess; thread.value = info.hThread;
    if (!AssignProcessToJobObject(job.value, process.value)) {
        TerminateProcess(process.value, 1); WaitForSingleObject(process.value, 1000);
        result.output = "Cannot attach command to its cancellation group; command was not executed."; return result;
    }
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
        TerminateJobObject(job.value, 1); result.output = "Cannot resume command."; return result;
    }
    result.started = true;
    write.close(); input.close(); thread.close();
    QByteArray output;
    const auto drain = [&] {
        // Bound each drain so continuously writing children cannot starve cancellation.
        for (int chunk = 0; chunk < 16; ++chunk) {
            DWORD available = 0, received = 0;
            if (!PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr) || !available) break;
            char buffer[4096];
            if (!ReadFile(read.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) || !received) break;
            const qsizetype keep = std::min<qsizetype>(received, 65536 - output.size());
            output.append(buffer, keep);
            if (keep < received) result.truncated = true;
        }
    };
    QElapsedTimer timer; timer.start();
    while (true) {
        drain();
        if (cancelled.load() || timer.elapsed() >= timeoutMs) {
            result.cancelled = cancelled.load(); result.timedOut = !result.cancelled;
            TerminateJobObject(job.value, 1); WaitForSingleObject(process.value, 1000); break;
        }
        const DWORD wait = WaitForSingleObject(process.value, 25);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) { TerminateJobObject(job.value, 1); break; }
    }
    GetExitCodeProcess(process.value, &result.exitCode);
    // Commands are foreground tasks: background descendants must not survive Stop/completion.
    job.close(); drain();
    result.output = decode(output, shell == "cmd");
    if (result.truncated) result.output += QStringLiteral("\n[Output truncated at 64 KiB]");
    return result;
}
QString ShellTool::defaultDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/JARVIS/commands";
}
ShellTool::ShellTool(bool diagnostics) : m_diagnostics(diagnostics) {
    m_definition.name = diagnostics ? "system_check" : "run_shell";
    m_definition.description = diagnostics ? "Run a fixed read-only diagnostic: network configuration, processes, disk space or Windows version." : "Execute a CMD or PowerShell command on Windows after explicit user confirmation. Runs without elevation, in a specified existing working directory or the JARVIS commands folder, with a 120-second limit. Output includes errors and exit status. Use system_check for simple diagnostics. Never claim a command ran until its result arrives.";
    m_definition.permission = diagnostics ? PermissionLevel::ReadOnly : PermissionLevel::ConfirmRequired;
    m_definition.timeoutMs = 125000;
    ArgumentSpec choice; choice.name = diagnostics ? "check" : "shell";
    choice.type = ArgumentType::Enumeration; choice.required = true;
    choice.allowedValues = diagnostics ? std::vector<std::string>{"network", "processes", "disk", "system"} : std::vector<std::string>{"cmd", "powershell"};
    m_definition.arguments.push_back(choice);
    if (!diagnostics) {
        ArgumentSpec command; command.name = "command"; command.type = ArgumentType::Text; command.required = true;
        m_definition.arguments.push_back(command);
        command.name = "working_directory"; command.required = false;
        m_definition.arguments.push_back(command);
    }
}
ToolResult ShellTool::execute(const ValidatedCall& call, const std::atomic<bool>& cancelled) {
    QString shell = QString::fromStdString(call.enumerationArgument("shell"));
    QString command = QString::fromStdString(call.textArgument("command"));
    QString directory = QString::fromStdString(call.textArgument("working_directory"));
    if (directory.isEmpty()) { directory = defaultDirectory(); if (!QDir{}.mkpath(directory)) return ToolResult::failure(call.toolName(), ToolErrorCode::ExecutionFailed, "Cannot create command working directory"); }
    if (m_diagnostics) {
        shell = "powershell";
        const auto check = call.enumerationArgument("check");
        if (check == "network") command = "Get-NetIPConfiguration | Format-List InterfaceAlias,IPv4Address,IPv4DefaultGateway,DNSServer; Write-Output ('ICMP 1.1.1.1 reachable: '+(Test-Connection -ComputerName 1.1.1.1 -Count 2 -Quiet))";
        else if (check == "processes") command = "Get-Process | Sort-Object CPU -Descending | Select-Object -First 15 ProcessName,Id,CPU,WorkingSet | Format-Table -AutoSize";
        else if (check == "disk") command = "Get-PSDrive -PSProvider FileSystem | Select-Object Name,Used,Free | Format-Table -AutoSize";
        else if (check == "system") command = "[System.Environment]::OSVersion.VersionString";
        else return ToolResult::failure(call.toolName(), ToolErrorCode::ValueNotAllowed, "Unknown diagnostic");
    }
    const auto result = runWindowsCommand(shell, command, directory, cancelled);
    if (result.cancelled) return ToolResult::failure(call.toolName(), ToolErrorCode::Cancelled, "Command stopped.\n" + result.output.toStdString());
    if (result.timedOut) return ToolResult::failure(call.toolName(), ToolErrorCode::Timeout, "Command exceeded 120 seconds and was stopped.\n" + result.output.toStdString());
    if (!result.started || result.exitCode != 0) return ToolResult::failure(call.toolName(), ToolErrorCode::ExecutionFailed,
        "Command failed (exit " + std::to_string(result.exitCode) + ").\n" + result.output.toStdString());
    return ToolResult::success(call.toolName(), {{"exit_code", "0"}, {"output", result.output.toStdString()}, {"working_directory", directory.toStdString()}});
}
}
