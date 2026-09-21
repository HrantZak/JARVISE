#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "CommandText.h"

namespace jarvis::app {

inline QString spotifySearchCall(QString query) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("spotify_search")},
        {QStringLiteral("arguments"), QJsonObject{
            {QStringLiteral("query"), query.trimmed()}
        }}
    }).toJson(QJsonDocument::Compact));
}

inline QString resolveSpotifyCommand(QString text) {
    const QString value = commandText(text);
    const QString provider = QStringLiteral("(?:spotify|спотифай|спотифая)");
    const QString verb = QStringLiteral("(?:найди|поищи|ищи|напиши|введи|включи)");

    auto match = QRegularExpression(
        QStringLiteral("^") + verb + QStringLiteral("\\s+(?:в\\s+(?:поиске\\s+)?|на\\s+)?")
            + provider + QStringLiteral("\\s+(.+)$"),
        QRegularExpression::CaseInsensitiveOption).match(value);
    if (match.hasMatch()) return spotifySearchCall(match.captured(1));

    match = QRegularExpression(
        QStringLiteral("^") + provider + QStringLiteral("\\s+") + verb + QStringLiteral("\\s+(.+)$"),
        QRegularExpression::CaseInsensitiveOption).match(value);
    if (match.hasMatch()) return spotifySearchCall(match.captured(1));

    // Natural phrasing: "открой Spotify и найди ...". Opening Spotify by
    // itself remains the normal application launch command.
    match = QRegularExpression(
        QStringLiteral("^открой\\s+") + provider
            + QStringLiteral("\\s+и\\s+") + verb + QStringLiteral("\\s+(.+)$"),
        QRegularExpression::CaseInsensitiveOption).match(value);
    if (match.hasMatch()) return spotifySearchCall(match.captured(1));

    return {};
}

} // namespace jarvis::app
