#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace jarvis::i18n {

/// The languages JARVIS speaks.
///
/// Russian and English are both primary: Russian is the default, and neither is
/// a translation layer bolted onto the other. Adding a language means adding an
/// entry here plus a .ts file - no other code changes.
enum class Language {
    Russian,
    English,
};

inline constexpr Language kDefaultLanguage = Language::Russian;

inline constexpr std::array<Language, 2> kSupportedLanguages{
    Language::Russian,
    Language::English,
};

/// ISO 639-1 code, used in config.json and in the .qm file name: "ru", "en".
[[nodiscard]] std::string_view languageCode(Language language) noexcept;

/// The language's own name for itself, for the language picker.
[[nodiscard]] std::string_view languageEndonym(Language language) noexcept;

/// Qt locale name: "ru_RU", "en_US".
[[nodiscard]] std::string_view qtLocaleName(Language language) noexcept;

/// Name of the compiled catalogue inside the binary: "jarvis_ru".
[[nodiscard]] std::string translationCatalogue(Language language);

/// Parses "ru", "RU", "ru_RU", "ru-RU". Returns nullopt for anything else, so a
/// typo in config.json is reported instead of silently becoming the default.
[[nodiscard]] std::optional<Language> languageFromCode(std::string_view code) noexcept;

} // namespace jarvis::i18n
