#include "jarvis/voice/SpeechText.h"
#include <QRegularExpression>
#include <QString>
#include <QXmlStreamWriter>

namespace jarvis::voice {
SpeechDelivery expressiveDelivery(std::string_view preparedText, i18n::Language language) {
    SpeechDelivery result{std::string(preparedText)};
    const QString text = QString::fromUtf8(preparedText.data(), static_cast<qsizetype>(preparedText.size())).trimmed();
    // Keep long technical explanations steady. These cues describe delivery,
    // not inferred emotional state or the success of a command.
    if (text.size() > 240 || text.contains(u'\\') || text.contains(QStringLiteral("http"))) return result;
    const bool russian = language == i18n::Language::Russian;
    const auto options = QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;
    const QRegularExpression caution(russian
        ? QStringLiteral("\\b(не удалось|не получилось|ошибка|осторожно|внимание|сожалею)\\b")
        : QStringLiteral("\\b(failed|could not|error|careful|sorry)\\b"), options);
    if (caution.match(text).hasMatch()) {
        result.lengthFactor = 1.025F;
        result.pauseFactor = 1.18F;
        return result;
    }
    if (text.endsWith(u'?')) {
        // Preserve the question mark for the model's native intonation.
        result.lengthFactor = 0.985F;
        result.pauseFactor = 1.10F;
        return result;
    }
    const QRegularExpression greeting(russian
        ? QStringLiteral("^(привет|здравствуйте|доброе утро|добрый день|добрый вечер|рад вас слышать|к вашим услугам|готово|отлично|разумеется)(?:,? сэр)?[.!]?$")
        : QStringLiteral("^(hello|good morning|good afternoon|good evening|at your service|ready|certainly|excellent)(?:,? sir)?[.!]?$"), options);
    if (greeting.match(text).hasMatch()) {
        result.lengthFactor = 0.96F;
        result.pauseFactor = 0.90F;
        QString lively = text;
        if (lively.endsWith(u'.')) lively.chop(1);
        if (!lively.endsWith(u'!')) lively += u'!';
        result.text = lively.toStdString();
    }
    return result;
}

std::string prepareSpeechText(std::string_view source, i18n::Language language) {
    QString text = QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size()));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    // Drop control characters forbidden by XML, keeping readable text literal.
    text.remove(QRegularExpression(QStringLiteral("[\\x{0}-\\x{8}\\x{B}\\x{C}\\x{E}-\\x{1F}\\x{FFFE}\\x{FFFF}]")));
    text.replace(QRegularExpression(QStringLiteral("\\[([^\\]\\n]+)\\]\\(https?://[^\\s)]+\\)")), QStringLiteral("\\1"));
    text.replace(QRegularExpression(QStringLiteral("(?m)^\\s{0,3}#{1,6}\\s+")), QString{});
    text.replace(QRegularExpression(QStringLiteral("(?m)^\\s*[-*•]\\s+")), QString{});
    text.replace(QRegularExpression(QStringLiteral("(?m)^```[^\\n]*$")), QString{});
    text.replace(QStringLiteral("**"), QString{});
    text.replace(QStringLiteral("__"), QString{});
    text.remove(u'`');
    if (language == i18n::Language::Russian) {
        // Whole-token matches prevent changing paths, identifiers and longer
        // words. Unknown words and numeric values remain with Windows' parser.
        const std::pair<const char*, const char*> aliases[] = {
            {"DeepSeek", "Дипсик"}, {"YouTube", "Ютуб"}, {"Spotify", "Спотифай"},
            {"PowerShell", "Пауэр шелл"}, {"Windows", "Виндоус"},
            {"Chrome", "Хром"}, {"Google", "Гугл"}, {"ChatGPT", "Чат джи пи ти"},
            {"NVIDIA", "Энвидиа"}, {"GeForce", "Джи форс"}, {"iPhone", "Айфон"},
            {"Bluetooth", "Блютус"}, {"Wi-Fi", "вай фай"}, {"Wi‑Fi", "вай фай"},
            {"USB", "ю эс би"}, {"HDMI", "эйч ди эм ай"}, {"SSD", "эс эс ди"},
            {"CPU", "си пи ю"}, {"GPU", "джи пи ю"}, {"API", "эй пи ай"},
            {"GPT", "джи пи ти"}, {"PDF", "пи ди эф"}, {"HTML", "эйч ти эм эл"},
            {"Python", "Пайтон"}, {"JavaScript", "Джава скрипт"}, {"GitHub", "Гитхаб"}
        };
        for (const auto& [word, pronunciation] : aliases) {
            const auto pattern = QStringLiteral("(?<![\\p{L}\\p{N}_./\\\\:])%1(?![\\p{L}\\p{N}_/\\\\]|\\.[\\p{L}\\p{N}])")
                .arg(QRegularExpression::escape(QString::fromUtf8(word)));
            text.replace(QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption),
                         QString::fromUtf8(pronunciation));
        }
        const std::pair<const char*, const char*> abbreviations[] = {
            {"т. е.", "то есть"}, {"т.е.", "то есть"},
            {"т. д.", "так далее"}, {"т.д.", "так далее"},
            {"т. п.", "тому подобное"}, {"т.п.", "тому подобное"}
        };
        for (const auto& [word, pronunciation] : abbreviations) {
            const auto pattern = QStringLiteral("(?<![\\p{L}\\p{N}_])%1(?![\\p{L}\\p{N}_])")
                .arg(QRegularExpression::escape(QString::fromUtf8(word)));
            text.replace(QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption),
                         QString::fromUtf8(pronunciation));
        }
    }
    text.replace(QRegularExpression(QStringLiteral("[\\t ]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed().toStdString();
}

std::string speechSsml(std::string_view source, i18n::Language language, std::string_view voiceLocale) {
    const auto plain = prepareSpeechText(source, language);
    QString xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement(QStringLiteral("speak"));
    writer.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2001/10/synthesis"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    writer.writeAttribute(QStringLiteral("xml:lang"), QString::fromUtf8(voiceLocale.data(), static_cast<qsizetype>(voiceLocale.size())));
    // Keep question marks and natural sentence punctuation. Paragraph markup
    // supplies phrase boundaries without splitting decimals, dates or initials.
    for (const auto& paragraph : QString::fromStdString(plain).split(u'\n', Qt::SkipEmptyParts)) {
        writer.writeTextElement(QStringLiteral("p"), paragraph.trimmed());
    }
    writer.writeEndElement();
    return xml.toStdString();
}
}
