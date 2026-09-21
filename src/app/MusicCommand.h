#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "CommandText.h"

namespace jarvis::app {

inline QString openMusicCall(QString query = {}) {
    QJsonObject arguments;
    if (!query.trimmed().isEmpty()) arguments[QStringLiteral("query")] = query.trimmed();
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("open_local_music")},
        {QStringLiteral("arguments"), arguments}
    }).toJson(QJsonDocument::Compact));
}

inline QString musicQueryFromSpeech(QString query) {
    query = query.simplified();
    if (query.isEmpty()) return {};

    // Speech recognition often appends the user's intent to a generic request:
    // "открой музыку ... хочу". Treat that as "open music" unless an actual
    // audio filename was spoken, so a filler word cannot become a filename.
    const bool hasIntentWord = QRegularExpression(
        QStringLiteral("(?:^|\\s)(?:хочу|пожалуйста|воспроизведи|включи)(?=\\s|[.,!?]|$)"),
        QRegularExpression::CaseInsensitiveOption).match(query).hasMatch();
    const bool hasAudioExtension = QRegularExpression(
        QStringLiteral("\\.(?:mp3|flac|wav|m4a|ogg|aac|wma)(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption).match(query).hasMatch();
    if (hasIntentWord && !hasAudioExtension) return {};
    return query;
}

inline QString resolveMusicCommand(QString text) {
    const QString value = commandText(text);
    auto match = QRegularExpression(
        QStringLiteral("^(?:открой|запусти|включи)\\s+(?:музыку|музыка|песню|трек|музыкальный\\s+файл)(?:\\s*[,!:;]?\\s+(.+))?$"),
        QRegularExpression::CaseInsensitiveOption).match(value);
    if (match.hasMatch()) return openMusicCall(musicQueryFromSpeech(match.captured(1)));

    match = QRegularExpression(
        QStringLiteral("^(?:открой|запусти|включи)\\s+(?:конкретный\\s+)?файл\\s+(.+)$"),
        QRegularExpression::CaseInsensitiveOption).match(value);
    if (match.hasMatch()) return openMusicCall(musicQueryFromSpeech(match.captured(1)));
    return {};
}

} // namespace jarvis::app
