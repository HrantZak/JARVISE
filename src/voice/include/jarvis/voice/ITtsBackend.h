#pragma once

#include <vector>

#include "jarvis/core/Result.h"
#include "jarvis/voice/VoiceTypes.h"

namespace jarvis::voice {

/// A local speech synthesiser.
///
/// Implementations must produce audio from the text they are given, every
/// time. Returning a pre-recorded clip, or silence dressed up as speech, would
/// make the whole subsystem a lie - if synthesis cannot happen, say so with an
/// error.
class ITtsBackend {
public:
    ITtsBackend() = default;
    virtual ~ITtsBackend() = default;

    ITtsBackend(const ITtsBackend&) = delete;
    ITtsBackend& operator=(const ITtsBackend&) = delete;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Whether a voice is installed for \p language.
    [[nodiscard]] virtual bool supports(i18n::Language language) const = 0;

    /// Name of the voice used for \p language, empty when none is installed.
    [[nodiscard]] virtual std::string voiceName(i18n::Language language) const = 0;

    /// Languages this backend can currently speak.
    [[nodiscard]] virtual std::vector<i18n::Language> availableLanguages() const = 0;

    /// Synthesises \p request. Blocking; called on a worker thread.
    [[nodiscard]] virtual core::Result<TtsResult> synthesize(const TtsRequest& request) = 0;

    /// Aborts a running synthesis as soon as it can.
    virtual void requestStop() noexcept = 0;
};

} // namespace jarvis::voice
