#pragma once
#include <QString>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
namespace jarvis::app {
inline QUrl spokenSearch(QString text) {
    text = text.trimmed();
    text.remove(QRegularExpression(QStringLiteral("^(?:джарвис|жарвис|джервис|джарис|джарвиз|джарвес|jarvis)[, :]*"), QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("^(?:пожалуйста[, ]+|можешь\\s+|можно\\s+|давай\\s+)+"), QRegularExpression::CaseInsensitiveOption));
    const QString lower = text.toLower();
    const bool google = lower.contains(QStringLiteral("гугл")) || lower.contains("google");
    // Watching a review does not require the user to name a search provider.
    // Keep requests to write or discuss a review in the conversation.
    const auto unicodeInsensitive = QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;
    const bool watchRequest = QRegularExpression(QStringLiteral("^(?:открой|открыть|найди|поищи|покажи|включи|посмотри|ищи|хочу\\s+(?:посмотреть|увидеть)|посмотреть)\\b"), unicodeInsensitive).match(text).hasMatch();
    const bool videoTopic = QRegularExpression(QStringLiteral("\\b(?:обзор\\w*|видео\\w*|ролик\\w*)\\b"), unicodeInsensitive).match(text).hasMatch();
    const bool youtube = lower.contains(QStringLiteral("ютуб")) || lower.contains("youtube") || lower.contains(QStringLiteral("ютюб")) || (!google && watchRequest && videoTopic);
    const bool information = lower.startsWith(QStringLiteral("информация про ")) || lower.startsWith(QStringLiteral("информацию про ")) || lower.startsWith(QStringLiteral("расскажи про ")) || lower.startsWith(QStringLiteral("найди информацию"));
    const bool request = watchRequest || QRegularExpression(QStringLiteral("^(?:открой|найди|поищи|хочу|покажи|включи|посмотри|ищи|информаци|расскажи|поиск|search|open)"), QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
    if ((!youtube && !google && !information) || !request) return {};
    QString query = text;
    query.remove(QRegularExpression(QStringLiteral("(?:на |в )?(?:ютубе?|ютюбе?|youtube|гугле?|google)"), QRegularExpression::CaseInsensitiveOption));
    query = query.simplified();
    query.remove(QRegularExpression(QStringLiteral("^(?:(?:открой|открыть|найди|поищи|хочу|покажи|включи|посмотри|ищи|посмотреть|увидеть|информацию|информация|расскажи|поиск|search|open|пожалуйста)(?:[,\\s]+|$))+"), QRegularExpression::CaseInsensitiveOption));
    query = query.simplified();
    query.remove(QRegularExpression(QStringLiteral("^(?:про|о)\\s+"), QRegularExpression::CaseInsensitiveOption));
    QUrl url(youtube ? "https://www.youtube.com/results" : "https://www.google.com/search");
    if (query.isEmpty()) return QUrl(youtube ? "https://www.youtube.com/" : "https://www.google.com/");
    QUrlQuery parameters;
    parameters.addQueryItem(youtube ? "search_query" : "q", query.left(500));
    url.setQuery(parameters);
    return url;
}
inline QString briefSpeech(QString text, QString question = {}) {
    text.remove(QRegularExpression(QStringLiteral("```[\\s\\S]*?```")));
    text.remove(QRegularExpression(QStringLiteral("https?://\\S+")));
    text.remove(QRegularExpression(QStringLiteral("[*#`]")));
    auto normalize=[](QString value){return value.toLower().remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]")));};
    const auto original=normalize(question);
    const auto parts=text.split(QRegularExpression(QStringLiteral("(?<=[.!?])\\s+|[\\r\\n]+")),Qt::SkipEmptyParts);
    for(auto part:parts) {
        part=part.simplified();
        const auto normalized=normalize(part);
        if(part.isEmpty() || (!original.isEmpty() && normalized==original)) continue;
        if(QRegularExpression(QStringLiteral("^(?:вы (?:спросили|хотите)|ты (?:спросил|хочешь)|ваш вопрос|вопрос:|давайте|рассмотрим|рассчитаем|сначала вычислим)"),QRegularExpression::CaseInsensitiveOption).match(part).hasMatch()) continue;
        if(part.contains(QRegularExpression(QStringLiteral("[=×÷∑√]|\\\\(?:frac|begin)|\\d\\s*[+*/^]\\s*\\d")))) continue;
        if(part.size()>140 || part.split(' ',Qt::SkipEmptyParts).size()>22) continue;
        return part;
    }
    return QStringLiteral("Подробности в чате.");
}
class WakeGate {
public:
    struct Result { bool accepted{false}; bool awakened{false}; QString command; };
    Result consume(QString text, qint64 now) {
        const auto match = QRegularExpression(QStringLiteral("^\\s*(?:джарвис|жарвис|джервис|джарис|джарвиз|джарвес|jarvis)\\b[,.:!?\\s]*"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption).match(text);
        if (match.hasMatch()) {
            text.remove(0, match.capturedLength());
            if (text.trimmed().isEmpty()) { until = now + 15000; return {false, true, {}}; }
            until = 0; return {true, false, text.trimmed()};
        }
        if (until > now && !text.trimmed().isEmpty()) { until = 0; return {true, false, text.trimmed()}; }
        return {};
    }
    void reset() { until = 0; }
private:
    qint64 until{0};
};
}
