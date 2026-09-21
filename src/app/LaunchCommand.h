#pragma once
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include "CommandText.h"
#include "jarvis/tools/ToolRegistry.h"
namespace jarvis::app {
struct LaunchCommand { bool matched{false}; QString call; QString error; };
inline LaunchCommand resolveLaunchCommand(QString text, const tools::ToolRegistry& registry) {
    // Polite or compound wording is left to the agent/planner. This keeps the
    // direct launcher narrow and prevents a model security test from turning a
    // free-form request into an implicit desktop action.
    if (text.contains(QStringLiteral("пожалуйста"), Qt::CaseInsensitive)) return {};
    text = commandText(text);
    text.remove(QRegularExpression(QStringLiteral("^(?:джарвис|жарвис|jarvis)[, :]*")));
    const auto match = QRegularExpression(QStringLiteral("^(?:открой|откри|запусти|open|launch|приложение)\\s+(?:приложение[: ]+|программу[: ]+)?(.+)$")).match(text);
    if (!match.hasMatch()) return {};
    QString name = match.captured(1).trimmed();
    name.remove(QRegularExpression(QStringLiteral("^(?:новое |новую |маленькое |маленькую )?(?:окно|окну)\\s+")));
    if (name == QStringLiteral("эту игру") || name == QStringLiteral("игру")) return {true, {}, QStringLiteral("Какую игру открыть, сэр? Назовите её название.")};
    if (name.startsWith(QStringLiteral("игру "))) name.remove(0, 5);
    name.replace(QStringLiteral("гта"), QStringLiteral("grand theft auto"));
    name.remove(QRegularExpression(QStringLiteral("[.!?]+$")));
    if(name==QStringLiteral("командную строку") || name=="cmd" || name==QStringLiteral("цмд")) name=QStringLiteral("командная строка");
    if(name=="powershell" || name==QStringLiteral("повершел") || name==QStringLiteral("пауэршелл")) name="windows powershell";
    const QMap<QString, QString> builtin{{"калькулятор", "calculator"}, {"calculator", "calculator"}, {"блокнот", "notepad"}, {"notepad", "notepad"}, {"проводник", "explorer"}, {"explorer", "explorer"}, {"настройки", "settings"}, {"settings", "settings"}};
    QString tool = "open_application";
    QString value = builtin.value(name);
    if (value.isEmpty()) {
    const QMap<QString, QString> aliases{{"гугл", "google chrome"}, {"google", "google chrome"}, {"chrome", "google chrome"}, {"хром", "google chrome"}, {"телеграм", "telegram"}, {"телеграмм", "telegram"}, {"дискорд", "discord"}, {"стим", "steam"}, {"спотифай", "spotify"}, {"spotify", "spotify"}};
        name = aliases.value(name, name);
        tool = "open_installed_application";
        if (const auto* installed = registry.lookup(tool.toStdString())) {
            QStringList candidates;
            for (const auto& arg : installed->definition().arguments)
                for (const auto& allowed : arg.allowedValues) {
                    const auto candidate = QString::fromStdString(allowed);
                    if (applicationKey(candidate) == applicationKey(name)) { value = candidate; break; }
                    if (applicationKey(candidate).contains(applicationKey(name))) candidates.append(candidate);
                }
            if (value.isEmpty() && candidates.size() == 1) value = candidates.first();
            if (value.isEmpty() && candidates.size() > 1) return {true, {}, QStringLiteral("Уточните название программы: ") + candidates.join(", ")};
        }
    }
    if (value.isEmpty()) return {true, {}, QStringLiteral("Не нашёл программу среди ярлыков меню «Пуск» и рабочего стола. Укажите точное название или добавьте ярлык.")};
    return {true, QString::fromUtf8(QJsonDocument(QJsonObject{{"tool", tool}, {"arguments", QJsonObject{{"application", value}}}}).toJson(QJsonDocument::Compact)), {}};
}
}
