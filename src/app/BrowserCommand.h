#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QMap>
#include "CommandText.h"

namespace jarvis::app {
inline QString browserCall(QString action, QString value = {}) {
    QJsonObject args;
    if (!value.isEmpty()) args[action == "open_url" ? "url" : "text"] = value.trimmed();
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"tool", "browser_" + action}, {"arguments", args}}).toJson(QJsonDocument::Compact));
}
inline QString browserLinkCall(int index) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"tool", "browser_open_link"}, {"arguments", QJsonObject{{"index", index}}}}).toJson(QJsonDocument::Compact));
}

inline QString resolveBrowserCommand(QString text) {
    const QString value = commandText(text);
    static const QMap<QString, int> ordinals{
        {"один",1},{"одна",1},{"одну",1},{"первую",1},{"первый",1},{"первая",1},
        {"два",2},{"две",2},{"вторую",2},{"второй",2},{"вторая",2},
        {"три",3},{"третью",3},{"третий",3},{"третья",3},
        {"четыре",4},{"четвертую",4},{"четвертый",4},{"четвертая",4},
        {"пять",5},{"пятую",5},{"пятый",5},{"пятая",5},
        {"шесть",6},{"шестую",6},{"шестой",6},{"шестая",6},
        {"семь",7},{"седьмую",7},{"седьмой",7},{"седьмая",7},
        {"восемь",8},{"восьмую",8},{"восьмой",8},{"восьмая",8},
        {"девять",9},{"девятую",9},{"девятый",9},{"девятая",9},
        {"десять",10},{"десятую",10},{"десятый",10},{"десятая",10}
    };
    const auto verb = QStringLiteral("(?:открой|откройте|перейди на|перейдите на|зайди в|зайди на|перейди в)");
    auto ordinal = QRegularExpression(QStringLiteral("^") + verb + QStringLiteral("\\s+(.+?)(?:\\s+ссылку|\\s+результат)?$"), QRegularExpression::CaseInsensitiveOption).match(value);
    if (ordinal.hasMatch()) {
        const QString phrase = ordinal.captured(1).trimmed();
        if (ordinals.contains(phrase)) return browserLinkCall(ordinals.value(phrase));
        bool ok=false; const int number=phrase.toInt(&ok);
        if (ok && number >= 1 && number <= 20) return browserLinkCall(number);
    }
    ordinal = QRegularExpression(QStringLiteral("^") + verb + QStringLiteral("\\s+(?:ссылку|результат)\\s+(?:номер\\s+)?(.+)$"), QRegularExpression::CaseInsensitiveOption).match(value);
    if (ordinal.hasMatch()) {
        const QString phrase=ordinal.captured(1).trimmed();
        bool ok=false; const int number=phrase.toInt(&ok);
        if (ok && number>=1 && number<=20) return browserLinkCall(number);
        if (ordinals.contains(phrase)) return browserLinkCall(ordinals.value(phrase));
    }
    if (value.startsWith(QStringLiteral("открой ссылку ")) || value.startsWith(QStringLiteral("открой "))) {
        const qsizetype scheme = value.indexOf(QStringLiteral("http://"));
        const qsizetype secure = value.indexOf(QStringLiteral("https://"));
        const qsizetype start = scheme >= 0 ? scheme : secure;
        if (start >= 0) return browserCall("open_url", value.mid(start).trimmed());
    }
    auto match = QRegularExpression(QStringLiteral("^(?:открой|перейди по|зайди по) (?:ссылке )?(https?://[^ ]+)$"), QRegularExpression::CaseInsensitiveOption).match(value);
    if (match.hasMatch()) return browserCall("open_url", match.captured(1));

    match = QRegularExpression(QStringLiteral("^(?:найди|поищи|напиши|введи) (?:в гугле|в google|в поиске) (.+)$"), QRegularExpression::CaseInsensitiveOption).match(value);
    if (match.hasMatch()) return browserCall("search", match.captured(1));

    match = QRegularExpression(QStringLiteral("^(?:напиши|введи|вставь) (?:в браузере|на странице|в поле) (.+)$"), QRegularExpression::CaseInsensitiveOption).match(value);
    if (match.hasMatch()) return browserCall("type", match.captured(1));

    if (value == QStringLiteral("нажми enter") || value == QStringLiteral("отправь поиск") || value == QStringLiteral("подтверди поиск"))
        return browserCall("submit");
    return {};
}
}
