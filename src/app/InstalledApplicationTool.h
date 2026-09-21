#pragma once
#include "jarvis/tools/ITool.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <windows.h>
#include <shellapi.h>
#include <map>
namespace jarvis::app {
class InstalledApplicationTool final : public tools::ITool {
public:
    InstalledApplicationTool() {
        m_definition.name = "open_installed_application";
        m_definition.description = "Open an app or game from desktop/Start Menu catalogue. Steam game shortcuts supported. Ask if ambiguous. No command-line arguments.";
        m_definition.permission = tools::PermissionLevel::SafeAction;
        m_definition.timeoutMs = 10000;
        tools::ArgumentSpec arg;
        arg.name = "application";
        arg.type = tools::ArgumentType::Enumeration;
        arg.description = "Installed application name";
        arg.required = true;
        const QString systemRoot=qEnvironmentVariable("SystemRoot", "C:/Windows");
        m_paths.emplace("Командная строка",(systemRoot+"/System32/cmd.exe").toStdWString());
        arg.allowedValues.push_back("Командная строка");
        const QStringList roots{QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), qEnvironmentVariable("PUBLIC") + "/Desktop", qEnvironmentVariable("APPDATA") + "/Microsoft/Windows/Start Menu/Programs", qEnvironmentVariable("PROGRAMDATA") + "/Microsoft/Windows/Start Menu/Programs"};
        for (const auto& root : roots) {
            QDirIterator files(root, {"*.lnk", "*.url"}, QDir::Files, QDirIterator::Subdirectories);
            while (files.hasNext() && m_paths.size() < 300) {
                const auto path = files.next();
                const auto name = QFileInfo(path).completeBaseName().toStdString();
                // Maintenance shortcuts are not ordinary app launches.
                if (QRegularExpression(QStringLiteral("uninstall|удален|удалить|деинстал|reset|сброс|cleanup|очистк|install|установ"), QRegularExpression::CaseInsensitiveOption).match(QString::fromStdString(name)).hasMatch()) continue;
                QString launchPath = path;
                if (path.endsWith(".url", Qt::CaseInsensitive)) {
                    QSettings shortcut(path, QSettings::IniFormat);
                    launchPath = shortcut.value("InternetShortcut/URL").toString();
                    if (!QRegularExpression("^steam://rungameid/[0-9]+$").match(launchPath).hasMatch()) continue;
                }
                if (m_paths.emplace(name, launchPath.toStdWString()).second) arg.allowedValues.push_back(name);
            }
        }
        if (arg.allowedValues.empty()) arg.allowedValues.push_back("No installed applications found");
        m_definition.arguments.push_back(std::move(arg));
    }
    const tools::ToolDefinition& definition() const override { return m_definition; }
    tools::ToolResult execute(const tools::ValidatedCall& call, const std::atomic<bool>& cancelled) override {
        if (cancelled.load()) return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::Cancelled, "Cancelled");
        const auto name = call.enumerationArgument("application");
        const auto found = m_paths.find(name);
        if (found == m_paths.end()) return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::ValueNotAllowed, "Application not found in catalogue");
        const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", found->second.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::ExecutionFailed, "Windows could not open the shortcut");
        return tools::ToolResult::success(call.toolName(), {{"application", name}, {"status", "launch requested"}});
    }
private:
    tools::ToolDefinition m_definition;
    std::map<std::string, std::wstring> m_paths;
};
}
