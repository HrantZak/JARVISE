#pragma once
#include <string>
#include <string_view>
#include "jarvis/i18n/Language.h"

namespace jarvis::voice {
// Speech-only rendering. Displayed answers, code and command arguments stay intact.
std::string prepareSpeechText(std::string_view text, i18n::Language language);
struct SpeechDelivery {
    std::string text;
    float lengthFactor{1.0F};
    float pauseFactor{1.0F};
};
// Small, deterministic phrasing adjustments; no random pitch modulation.
SpeechDelivery expressiveDelivery(std::string_view preparedText, i18n::Language language);
std::string speechSsml(std::string_view text, i18n::Language language,
                       std::string_view voiceLocale);
}
