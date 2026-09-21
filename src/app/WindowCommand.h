#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include "CommandText.h"
namespace jarvis::app {
inline QString resolveWindowCommand(QString text) {
    text = commandText(text);
    text.remove(QRegularExpression(QStringLiteral("^(?:джарвис|жарвис|jarvis)[, :]*")));
    text.remove(QRegularExpression(QStringLiteral("[.!?]+$")));
    const auto make = [](QString action, QString target, int monitor = 0) {
        QJsonObject args;
        if (action != "list") args["target"] = target.trimmed();
        if (monitor) args["monitor"] = monitor;
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"tool", "window_" + action}, {"arguments", args}}).toJson(QJsonDocument::Compact));
    };
    if (text == QStringLiteral("покажи открытые окна") || text == QStringLiteral("список окон")) return make("list", {});
    auto m = QRegularExpression(QStringLiteral("^закрой вс[её] (?:окна|окно)(?:,)? кроме (?:приложения )?(.+)$")).match(text);
    if(m.hasMatch()) return make("close_except",m.captured(1));
    m = QRegularExpression(QStringLiteral("^открой (?:(?:новую|чистую|пустую) )*вкладку(?: в (.+))?$")).match(text);
    if(m.hasMatch()) return make("new_tab",m.captured(1).isEmpty() ? "chrome" : m.captured(1));
    m = QRegularExpression(QStringLiteral("^закрой (?:(?:текущую|последнюю|эту|активную) )?вкладку(?: в (.+))?$")).match(text);
    if(m.hasMatch()) return make("close_tab",m.captured(1).isEmpty() ? "current" : m.captured(1));
    m = QRegularExpression(QStringLiteral("^(?:перенеси|перемести) (.+) на (?:монитор )?(второй|первый|[1-9])(?:[- ]?(?:й|ой))?(?: (?:монитор|моник))?$")).match(text);
    if(m.hasMatch()) return make("move",m.captured(1),m.captured(2)==QStringLiteral("второй") ? 2 : m.captured(2)==QStringLiteral("первый") ? 1 : m.captured(2).toInt());
    m = QRegularExpression(QStringLiteral("^(?:разверни|сделай) (.+) (?:на весь экран|на полный экран|полный экран)$")).match(text);
    if(m.hasMatch()) return make("maximize",m.captured(1));
    if(text == QStringLiteral("сделай полный экран")) return make("maximize","current");
    m = QRegularExpression(QStringLiteral("^полный экран (.+)$")).match(text);
    if(m.hasMatch()) return make("maximize",m.captured(1));
    m = QRegularExpression(QStringLiteral("^(?:включи|выключи) полный экран (.+)$")).match(text);
    if(m.hasMatch()) return make("fullscreen",m.captured(1));
    if (text == QStringLiteral("закрой окно") || text == QStringLiteral("закрой окном") || text == QStringLiteral("закрой это окно") || text == QStringLiteral("закрой своё окно") || text == QStringLiteral("закрой свое окно")) return make("close","current");
    m = QRegularExpression(QStringLiteral("^закрой (?:последнее|текущее|это) окно(?: в (.+))?$")).match(text);
    if(m.hasMatch()) return make("close",m.captured(1).isEmpty() ? "current" : m.captured(1));
    m = QRegularExpression(QStringLiteral("^(закрой|сверни|разверни|восстанови) (.+)$")).match(text);
    if(m.hasMatch()) {
        QString target=m.captured(2); if(target.startsWith(QStringLiteral("окно "))) target.remove(0,5);
        if (target == QStringLiteral("окном") || target == QStringLiteral("своё окно") || target == QStringLiteral("свое окно")) target = QStringLiteral("current");
        if (target == QStringLiteral("калькулятор") || target == QStringLiteral("calculator")) return {};
        return make(m.captured(1)==QStringLiteral("закрой") ? "close" : m.captured(1)==QStringLiteral("сверни") ? "minimize" : m.captured(1)==QStringLiteral("разверни") ? "maximize" : "restore",target);
    }
    return {};
}
}
