#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include "jarvis/voice/ITtsBackend.h"

namespace jarvis::voice {

/// Windows built-in voices. Audio stays in memory; no Python or GPU model.
class WindowsTtsBackend final : public ITtsBackend {
public:
    WindowsTtsBackend();
    std::string_view name() const noexcept override { return "Windows"; }
    bool supports(i18n::Language language) const override;
    std::string voiceName(i18n::Language language) const override;
    std::vector<i18n::Language> availableLanguages() const override;
    core::Result<TtsResult> synthesize(const TtsRequest& request) override;
    void requestStop() noexcept override { m_epoch.fetch_add(1); }

private:
    struct Voice { std::string id; std::string name; int score; };
    std::map<i18n::Language, Voice> m_voices;
    std::atomic<std::uint64_t> m_epoch{0};
    std::mutex m_synthesisMutex;
};
} // namespace jarvis::voice
