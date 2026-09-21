#pragma once
#include <QString>
#include <QRegularExpression>
namespace jarvis::app {
inline QString commandText(QString text) {
    text=text.simplified().toLower();
    text.remove(QRegularExpression(QStringLiteral("^(?:(?:джарвис|жарвис|джервис|джарис|джарвиз|джарвес|jarvis|ну-ка|ну ка|пожалуйста|давай|можешь)[, :!]*\\s*)+")));
    text.remove(QRegularExpression(QStringLiteral("[.!?]+$")));
    return text.trimmed();
}
inline QString applicationKey(QString text) {
    text=commandText(text);
    text.replace(QRegularExpression(QStringLiteral("\\b(?:гта|gta)\\b"),QRegularExpression::UseUnicodePropertiesOption),"grand theft auto");
    text.replace(QRegularExpression("\\bv\\b"),"5");
    text.replace(QRegularExpression(QStringLiteral("\\b(?:пять|пятую|пятая)\\b"),QRegularExpression::UseUnicodePropertiesOption),"5");
    text.replace(QRegularExpression(QStringLiteral("\\b(?:легаси|legacy)\\b"),QRegularExpression::UseUnicodePropertiesOption),"legacy");
    text.replace(QRegularExpression("[^\\p{L}\\p{N}]+")," ");
    return text.simplified();
}
}
