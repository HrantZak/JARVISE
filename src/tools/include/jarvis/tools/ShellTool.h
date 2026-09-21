#pragma once
#include "jarvis/tools/ITool.h"
#include <QString>
namespace jarvis::tools {
struct CommandOutcome {
    bool started{false}, cancelled{false}, timedOut{false}, truncated{false};
    unsigned long exitCode{1};
    QString output;
};
// Exposed for process-lifetime and encoding tests, not a QML entry point.
CommandOutcome runWindowsCommand(const QString& shell, const QString& command,
    const QString& directory, const std::atomic<bool>& cancelled, int timeoutMs = 120000);
class ShellTool final : public ITool {
public:
    explicit ShellTool(bool diagnostics = false);
    const ToolDefinition& definition() const override { return m_definition; }
    ToolResult execute(const ValidatedCall&, const std::atomic<bool>&) override;
    static QString defaultDirectory();
private:
    bool m_diagnostics;
    ToolDefinition m_definition;
};
}
