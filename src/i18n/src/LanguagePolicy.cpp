#include "jarvis/i18n/LanguagePolicy.h"

#include <format>

namespace jarvis::i18n {

LanguagePolicy::LanguagePolicy(Language language) noexcept
    : m_language{language} {}

std::string_view LanguagePolicy::replyLanguageName() const noexcept {
    switch (m_language) {
    case Language::Russian: return "Russian";
    case Language::English: return "English";
    }
    return "Russian";
}

std::string LanguagePolicy::systemPromptSection() const {
    // Wording notes, because these lines are load-bearing:
    //  - the default reply language is stated first and unconditionally;
    //  - comprehension is stated separately from production, so a Russian
    //    question never forces an English answer or the reverse;
    //  - proper nouns are protected explicitly, otherwise models cheerfully
    //    translate "Visual Studio" into "Визуальная студия" and the application
    //    launcher can no longer match anything;
    //  - the user's explicit request always wins over the configured default.
    return std::format(
        "LANGUAGE\n"
        "- Reply in {0} by default.\n"
        "- Understand Russian and English equally well, including a mixture of "
        "the two in one sentence, and including casual or colloquial speech.\n"
        "- The language the user writes in does not change the reply language: "
        "keep replying in {0} unless the user explicitly asks otherwise.\n"
        "- If the user explicitly asks for a reply in another language, comply "
        "for as long as they want it.\n"
        "- Never translate proper names. Application names, file names, folder "
        "names, paths, commands, model names and tool names stay exactly as "
        "they are written.\n"
        "- Keep established technical abbreviations in their usual form "
        "(CPU, GPU, RAM, VRAM, SSD).\n"
        "- Use the typographic conventions of the reply language, including "
        "Russian quotation marks and the letter ё where it belongs.\n",
        replyLanguageName());
}

} // namespace jarvis::i18n
