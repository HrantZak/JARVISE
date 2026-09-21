#pragma once
#include "jarvis/tools/ITool.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>
namespace jarvis::app {
inline QString normalizedMusicName(QString value) {
    value=value.toLower();
    value.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+"), QRegularExpression::UseUnicodePropertiesOption));
    return value;
}
class MusicLibraryTool final : public tools::ITool {
    tools::ToolDefinition def;
public:
    explicit MusicLibraryTool(bool open=false) {
        def.name=open ? "open_local_music" : "list_local_music";
        def.description=open
            ? "Open a local audio file in the Windows default music player. Search only Music, Downloads and Desktop; query is a filename or partial name. Does not upload contents or scan whole disks."
            : "List local music filenames in Music, Downloads and Desktop. Does not upload contents or scan whole disks. Bounded to 200 results and 2 seconds.";
        def.permission=open ? tools::PermissionLevel::SafeAction : tools::PermissionLevel::ReadOnly;
        def.timeoutMs=4000;
        if (open) {
            tools::ArgumentSpec query;
            query.name="query"; query.type=tools::ArgumentType::Text; query.required=false; query.maxTextChars=260;
            def.arguments.push_back(query);
        }
    }
    const tools::ToolDefinition& definition() const override{return def;}
    tools::ToolResult execute(const tools::ValidatedCall& call,const std::atomic<bool>& cancelled) override {
        QElapsedTimer timer; timer.start(); QStringList found, roots; QSet<QString> seen;
        const QStringList types{"*.mp3","*.flac","*.wav","*.m4a","*.ogg","*.aac","*.wma"};
        bool limited=false;
        QString query;
        if (def.name=="open_local_music") query=QString::fromStdString(call.textArgument("query")).trimmed();
        QString exactMatch, partialMatch;
        for(auto location : {QStandardPaths::MusicLocation,QStandardPaths::DownloadLocation,QStandardPaths::DesktopLocation}) {
            const auto root=QStandardPaths::writableLocation(location); if(root.isEmpty() || roots.contains(root))continue; roots<<root;
            QDirIterator files(root,types,QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
            while(files.hasNext()) {
                if(cancelled.load()) return tools::ToolResult::failure(call.toolName(),tools::ToolErrorCode::Cancelled,"Cancelled");
                if(timer.elapsed()>2000 || found.size()>=200){limited=true;break;}
                const auto path=files.next();
                if(!seen.contains(path)) {
                    seen.insert(path); found<<QDir::toNativeSeparators(path);
                    if (def.name=="open_local_music" && !query.isEmpty()) {
                        const QString fileName=QFileInfo(path).fileName();
                        const QString baseName=QFileInfo(path).completeBaseName();
                        const QString wanted=QFileInfo(query).fileName();
                        const QString normalizedFile=normalizedMusicName(fileName);
                        const QString normalizedBase=normalizedMusicName(baseName);
                        const QString normalizedWanted=normalizedMusicName(wanted);
                        const QString normalizedQuery=normalizedMusicName(query);
                        if (fileName.compare(wanted,Qt::CaseInsensitive)==0 || baseName.compare(query,Qt::CaseInsensitive)==0 || normalizedFile==normalizedWanted || normalizedBase==normalizedQuery)
                            exactMatch=path;
                        else if (partialMatch.isEmpty() && (fileName.contains(query,Qt::CaseInsensitive) || baseName.contains(query,Qt::CaseInsensitive) || normalizedFile.contains(normalizedQuery) || normalizedBase.contains(normalizedQuery)))
                            partialMatch=path;
                    }
                }
            }
            if(limited)break;
        }
        if (def.name=="open_local_music") {
            QString selected = !exactMatch.isEmpty() ? exactMatch : partialMatch;
            if (selected.isEmpty() && query.isEmpty() && !found.isEmpty()) {
                std::ranges::sort(found, [](const QString& left, const QString& right) {
                    return QFileInfo(left).lastModified() > QFileInfo(right).lastModified();
                });
                selected=found.first();
            }
            if (selected.isEmpty()) {
                return tools::ToolResult::failure(call.toolName(),tools::ToolErrorCode::ExecutionFailed,
                    query.isEmpty() ? "Музыкальные файлы не найдены в папках Музыка, Загрузки и Рабочий стол."
                                    : "Музыкальный файл с таким названием не найден.");
            }
            const QUrl url=QUrl::fromLocalFile(selected);
            if (!QDesktopServices::openUrl(url))
                return tools::ToolResult::failure(call.toolName(),tools::ToolErrorCode::ExecutionFailed,"Windows не смогла открыть файл в музыкальном плеере.");
            return tools::ToolResult::success(call.toolName(),{{"status","Музыка открыта в плеере."},{"path",QDir::toNativeSeparators(selected).toStdString()}});
        }
        return tools::ToolResult::success(call.toolName(),{{"files",found.join('\n').toStdString()},{"count",std::to_string(found.size())},{"folders",roots.join(", ").toStdString()},{"limited",limited?"true":"false"}});
    }
};
}
