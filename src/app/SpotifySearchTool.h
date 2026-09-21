#pragma once

#include "jarvis/tools/ITool.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QStringList>
#include <utility>

namespace jarvis::app {

class SpotifySearchTool final : public tools::ITool {
    tools::ToolDefinition m_definition;

public:
    SpotifySearchTool() {
        m_definition.name = "spotify_search";
        m_definition.description =
            "Open a Spotify search for a spoken query. Uses the installed Spotify app when available and falls back to the Spotify web search. Does not play or purchase anything.";
        m_definition.permission = tools::PermissionLevel::SafeAction;
        m_definition.timeoutMs = 10000;

        tools::ArgumentSpec query;
        query.name = "query";
        query.type = tools::ArgumentType::Text;
        query.required = true;
        query.maxTextChars = 500;
        m_definition.arguments.push_back(std::move(query));
    }

    const tools::ToolDefinition& definition() const override { return m_definition; }

    tools::ToolResult execute(const tools::ValidatedCall& call,
                              const std::atomic<bool>& cancelled) override {
        if (cancelled.load()) {
            return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::Cancelled,
                                               "Cancelled");
        }

        const QString query = QString::fromStdString(call.textArgument("query")).simplified();
        if (query.isEmpty()) {
            return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::ExecutionFailed,
                                               "Поисковый запрос Spotify пуст.");
        }

        const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(query));
        const QString spotifyUri = QStringLiteral("spotify:search:%1").arg(encoded);

        // The classic desktop client accepts a Spotify URI as its command-line
        // argument even when Windows has no spotify: protocol association.
        const QStringList executableCandidates{
            qEnvironmentVariable("APPDATA") + QStringLiteral("/Spotify/Spotify.exe"),
            qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Spotify/Spotify.exe"),
            qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Spotify/Spotify.exe"),
            qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/Spotify/Spotify.exe")
        };
        for (const QString& executable : executableCandidates) {
            if (QFileInfo::exists(executable)
                && QProcess::startDetached(executable, {spotifyUri})) {
                return tools::ToolResult::success(call.toolName(), {
                    {"status", "Поиск Spotify открыт в приложении."},
                    {"query", query.toStdString()}
                });
            }
        }

        const QUrl appUrl(QStringLiteral("spotify:search:%1").arg(encoded));
        if (QDesktopServices::openUrl(appUrl)) {
            return tools::ToolResult::success(call.toolName(), {
                {"status", "Поиск Spotify открыт в приложении."},
                {"query", query.toStdString()}
            });
        }

        const QUrl webUrl(QStringLiteral("https://open.spotify.com/search/%1").arg(encoded));
        if (!QDesktopServices::openUrl(webUrl)) {
            return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::ExecutionFailed,
                                               "Windows не смогла открыть Spotify.");
        }
        return tools::ToolResult::success(call.toolName(), {
            {"status", "Поиск Spotify открыт в браузере."},
            {"query", query.toStdString()}
        });
    }
};

} // namespace jarvis::app
