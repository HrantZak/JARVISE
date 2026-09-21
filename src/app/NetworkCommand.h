#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

#include "CommandText.h"

namespace jarvis::app {

/// Fast path for common spoken requests. The tool remains available to the
/// model for natural variants, while these phrases avoid a needless LLM round
/// trip when the user plainly asks who is on the local network.
inline QString resolveNetworkDiscoveryCommand(const QString& text) {
    const QString command = commandText(text);
    const auto options = QRegularExpression::CaseInsensitiveOption
                         | QRegularExpression::UseUnicodePropertiesOption;
    const bool networkContext = QRegularExpression(
        QStringLiteral("(?:подключ|сет|wi[- ]?fi|вайфай|интернет)"), options)
        .match(command).hasMatch();
    const bool asksDevices = networkContext && QRegularExpression(
        QStringLiteral("(?:кто|какие|сколько|покажи|найди|проверь).*(?:подключ|устройств|телефон|девайс)|(?:кто|что).*(?:подключ|сет|wi[- ]?fi|вайфай)"), options)
        .match(command).hasMatch();
    const bool asksNetwork = QRegularExpression(
        QStringLiteral("(?:устройств|телефон|девайс).*(?:локальн|домашн|одн|этой).*(?:сет|wi[- ]?fi|вайфай)"), options)
        .match(command).hasMatch();
    if (!asksDevices && !asksNetwork) {
        return {};
    }
    return QString::fromUtf8(QJsonDocument(
        QJsonObject{{QStringLiteral("tool"), QStringLiteral("network_discovery")},
                    {QStringLiteral("arguments"), QJsonObject{}}})
        .toJson(QJsonDocument::Compact));
}

} // namespace jarvis::app
