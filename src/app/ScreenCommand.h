#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

#include "CommandText.h"

namespace jarvis::app {

/// Fast path for an explicit spoken request to inspect what is visible. A
/// screenshot is never taken from an ordinary question or in the background.
inline QString resolveScreenCommand(QString text) {
    const QString value = commandText(text);
    const auto options = QRegularExpression::CaseInsensitiveOption
                         | QRegularExpression::UseUnicodePropertiesOption;
    if (QRegularExpression(QStringLiteral(
            "(?:не\\s+смотр|не\\s+анализ|не\\s+сним|отмен)"), options)
            .match(value).hasMatch()) {
        return {};
    }
    if (QRegularExpression(QStringLiteral("^(?:сделай|сохрани|сними|создай)\\s+(?:скриншот|скрин|снимок экрана)(?:\\s.*)?$"), options).match(value).hasMatch()) {
        const QString target = value.contains(QStringLiteral("окна")) ? "active_window" : "screen";
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"tool", "take_screenshot"},
            {"arguments", QJsonObject{{"target", target}}}}).toJson(QJsonDocument::Compact));
    }
    const auto recording = QRegularExpression(QStringLiteral("^(начни|включи|запусти|останови|выключи|заверши|отключи)\\s+запись(?:\\s+экрана)?$"), options).match(value);
    if (recording.hasMatch()) {
        const QString verb = recording.captured(1);
        const bool start = verb == QStringLiteral("начни") || verb == QStringLiteral("включи") || verb == QStringLiteral("запусти");
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"tool", "windows_recording"}, {"arguments", QJsonObject{{"action", start ? "start" : "stop"}}}}).toJson(QJsonDocument::Compact));
    }
    const bool asksToLook = QRegularExpression(QStringLiteral(
        "(?:смотр|посмотр|видишь|видит|проанализ|прочитай|реши|что\\s+(?:на|видно\\s+на)\\s+экране)"), options)
        .match(value).hasMatch();
    if (!asksToLook) {
        return {};
    }

    const bool activeWindow = QRegularExpression(QStringLiteral(
        "(?:активн|текущ|это\\s+окно|вот\\s+сюда|в\\s+окне|на\\s+код|на\\s+файл|на\\s+пример)"), options)
        .match(value).hasMatch();
    const QString target = activeWindow ? QStringLiteral("active_window")
                                        : QStringLiteral("screen");
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("screen_analyze")},
        {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("target"), target}}}
    }).toJson(QJsonDocument::Compact));
}

} // namespace jarvis::app
