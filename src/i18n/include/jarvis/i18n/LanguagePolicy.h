#pragma once

#include <string>

#include "jarvis/i18n/Language.h"

namespace jarvis::i18n {

/// The language rules handed to the local model.
///
/// This is the seam Phase 3 plugs into: LanguagePolicy turns the user's chosen
/// interface language into the block of instructions that will be appended to
/// the LLM system prompt. It is pure text generation with no model behind it
/// yet, which is exactly why it can be written and tested now - when the
/// llama.cpp backend arrives it consumes this rather than inventing its own
/// wording.
///
/// The directive itself is written in English on purpose: instruction-following
/// is measurably more reliable in English across the open-weight models JARVIS
/// targets, even when the model is being told to answer in Russian.
class LanguagePolicy {
public:
    explicit LanguagePolicy(Language language = kDefaultLanguage) noexcept;

    [[nodiscard]] Language language() const noexcept { return m_language; }
    void setLanguage(Language language) noexcept { m_language = language; }

    /// The language section of the system prompt.
    [[nodiscard]] std::string systemPromptSection() const;

    /// Full name of the reply language as the model should understand it,
    /// e.g. "Russian".
    [[nodiscard]] std::string_view replyLanguageName() const noexcept;

private:
    Language m_language;
};

} // namespace jarvis::i18n
