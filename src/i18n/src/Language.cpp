#include "jarvis/i18n/Language.h"

#include <algorithm>
#include <cctype>

namespace jarvis::i18n {
namespace {

struct Entry {
    Language language;
    std::string_view code;
    std::string_view endonym;
    std::string_view locale;
};

// The endonym is written as literal UTF-8. The whole project compiles with
// /utf-8, so source and execution charset are both UTF-8 and no escaping games
// are needed anywhere in JARVIS.
constexpr std::array<Entry, 2> kEntries{{
    {Language::Russian, "ru", "Русский", "ru_RU"},
    {Language::English, "en", "English", "en_US"},
}};

const Entry& entryFor(Language language) noexcept {
    for (const Entry& entry : kEntries) {
        if (entry.language == language) {
            return entry;
        }
    }
    return kEntries.front();
}

} // namespace

std::string_view languageCode(Language language) noexcept {
    return entryFor(language).code;
}

std::string_view languageEndonym(Language language) noexcept {
    return entryFor(language).endonym;
}

std::string_view qtLocaleName(Language language) noexcept {
    return entryFor(language).locale;
}

std::string translationCatalogue(Language language) {
    return std::string{"jarvis_"} + std::string{languageCode(language)};
}

std::optional<Language> languageFromCode(std::string_view code) noexcept {
    // Accept "ru", "RU", "ru_RU" and "ru-RU": all four turn up in config files
    // and in Qt locale names.
    std::string normalised;
    for (const char ch : code) {
        if (ch == '_' || ch == '-') {
            break;
        }
        normalised.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }

    for (const Entry& entry : kEntries) {
        if (entry.code == normalised) {
            return entry.language;
        }
    }
    return std::nullopt;
}

} // namespace jarvis::i18n
