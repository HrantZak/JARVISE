#pragma once
#include "jarvis/tools/ITool.h"
#include <QString>
#include <QByteArray>
namespace jarvis::tools {
QByteArray makeWordDocument(const QString& title, const QString& content);
class CreateArtifactTool final : public ITool {
public:
    enum class Kind { Folder, Text, Word };
    explicit CreateArtifactTool(Kind kind, QString root = {});
    const ToolDefinition& definition() const override { return m_definition; }
    ToolResult execute(const ValidatedCall&, const std::atomic<bool>&) override;
    static QString defaultRoot();
private:
    Kind m_kind;
    QString m_root;
    ToolDefinition m_definition;
};
}
