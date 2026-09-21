#include "jarvis/config/AppConfig.h"

#include <algorithm>
#include <format>

namespace jarvis::config {
namespace {

/// Clamps \p value into [lo, hi]; records a message when it had to move.
template <typename T>
void clampField(T& value,
                T lo,
                T hi,
                std::string_view name,
                ValidationReport& report) {
    const T original = value;
    value = std::clamp(value, lo, hi);
    if (value != original) {
        report.adjustments.push_back(
            std::format("{}: {} is out of range [{}, {}], using {}",
                        name, original, lo, hi, value));
    }
}

} // namespace

ValidationReport validate(AppConfig& config) {
    ValidationReport report;

    if (config.configVersion < 1) {
        report.adjustments.push_back(
            std::format("configVersion: {} is invalid, using {}",
                        config.configVersion, kCurrentConfigVersion));
        config.configVersion = kCurrentConfigVersion;
    } else if (config.configVersion < kCurrentConfigVersion) {
        // Every version bump so far has only added keys, so missing ones simply
        // take their defaults. The file is stamped with the new version on the
        // next save. A future bump that changes the meaning of a key must
        // migrate here instead.
        report.adjustments.push_back(
            std::format("configVersion: upgraded {} -> {}, new settings take their defaults",
                        config.configVersion, kCurrentConfigVersion));
        config.configVersion = kCurrentConfigVersion;
    }

    // Logging
    clampField(config.logging.retentionDays, 1, 365, "logging.retentionDays", report);
    clampField(config.logging.maxFileBytes,
               static_cast<std::size_t>(64u * 1024u),
               static_cast<std::size_t>(256u * 1024u * 1024u),
               "logging.maxFileBytes",
               report);

    // Window. The lower bound is the smallest size the Phase 1 layout stays
    // usable at; the upper bound guards against a corrupted value creating a
    // window no monitor can show.
    clampField(config.window.width, 960, 7680, "window.width", report);
    clampField(config.window.height, 600, 4320, "window.height", report);

    // LLM. The ceilings are what an 8 GB card driving a desktop can actually
    // sustain; the floors are what produces a usable answer at all.
    if (config.llm.responseMode != "balanced" && config.llm.responseMode != "fast" &&
        config.llm.responseMode != "thorough") {
        config.llm.responseMode = "balanced";
        report.adjustments.push_back("llm.responseMode: using balanced");
    }
    clampField(config.voice.sttThreads, 1, 8, "voice.sttThreads", report);
    clampField(config.llm.gpuLayers, -1, 999, "llm.gpuLayers", report);
    clampField(config.llm.contextLength, 512U, 131072U, "llm.contextLength", report);
    clampField(config.llm.threads, 0U, 256U, "llm.threads", report);
    clampField(config.llm.batchSize, 32U, 4096U, "llm.batchSize", report);
    clampField(config.llm.temperature, 0.0F, 2.0F, "llm.temperature", report);
    clampField(config.llm.topP, 0.0F, 1.0F, "llm.topP", report);
    clampField(config.llm.topK, 0, 1000, "llm.topK", report);
    clampField(config.llm.maxTokens, 1, 32768, "llm.maxTokens", report);

    // Voice. The VAD thresholds are the ones a user is most likely to break by
    // hand-editing: a release threshold above the activation threshold would
    // make speech end the instant it started.
    clampField(config.voice.vadActivationThreshold, 0.001F, 0.5F,
               "voice.vadActivationThreshold", report);
    clampField(config.voice.vadReleaseThreshold, 0.0005F, 0.5F,
               "voice.vadReleaseThreshold", report);

    if (config.voice.vadReleaseThreshold >= config.voice.vadActivationThreshold) {
        const float corrected = config.voice.vadActivationThreshold * 0.5F;
        report.adjustments.push_back(
            std::format("voice.vadReleaseThreshold: {} is not below the activation "
                        "threshold {}, using {}",
                        config.voice.vadReleaseThreshold,
                        config.voice.vadActivationThreshold, corrected));
        config.voice.vadReleaseThreshold = corrected;
    }

    clampField(config.voice.vadSilenceTimeoutMs, 100, 10000,
               "voice.vadSilenceTimeoutMs", report);
    clampField(config.voice.vadMinimumSpeechMs, 50, 5000,
               "voice.vadMinimumSpeechMs", report);
    clampField(config.voice.vadMaximumUtteranceMs, 1000, 120000,
               "voice.vadMaximumUtteranceMs", report);
    clampField(config.voice.vadPreRollMs, 0, 2000, "voice.vadPreRollMs", report);
    clampField(config.voice.ttsLengthScale, 0.25F, 4.0F, "voice.ttsLengthScale", report);
    clampField(config.voice.ttsVolume, 0.0F, 1.0F, "voice.ttsVolume", report);

    // Tool limits. The upper bounds are not politeness: a config claiming 10000
    // rounds or a ten-minute confirmation window is either a typo or an attempt
    // to weaken the loop bound, and neither should be honoured.
    clampField(config.tools.maxRounds, 1, 16, "tools.maxRounds", report);
    clampField(config.tools.confirmationTimeoutMs, 5000, 300000,
               "tools.confirmationTimeoutMs", report);
    clampField(config.tools.auditCapacity, 100, 100000, "tools.auditCapacity", report);

    // Agent limits. The upper bounds mirror the ceilings in the code that owns
    // each structure - the plan size in Task, the tool-result size in
    // ContextManager, the retry count in AppConfig.h. A config file cannot
    // widen the agent's reach; asking for a hundred steps gets it thirty-two.
    clampField(config.agent.maxSteps, 1, 32, "agent.maxSteps", report);
    clampField(config.agent.maxRetries, 0, kMaxRetryCeiling, "agent.maxRetries", report);
    clampField(config.agent.maxToolCalls, 1, 64, "agent.maxToolCalls", report);
    clampField(config.agent.maxRuntimeMs, 5000, 1800000, "agent.maxRuntimeMs", report);
    clampField(config.agent.maxToolResultChars, 256, 4096, "agent.maxToolResultChars",
               report);
    clampField(config.agent.maxMemoryEntries, 0, 1024, "agent.maxMemoryEntries", report);
    clampField(config.agent.maxMemoryEntryChars, 32, 4096, "agent.maxMemoryEntryChars",
               report);

    if (config.agent.memoryPersistent && !config.agent.memoryEnabled) {
        // Persistence without memory is a setting that cannot mean anything.
        // Reporting it beats honouring half of it.
        report.adjustments.push_back(
            "agent.memoryPersistent: memory is disabled, so persistence is off");
        config.agent.memoryPersistent = false;
    }

    const bool xUnset = config.window.x == WindowSettings::kUnsetPosition;
    const bool yUnset = config.window.y == WindowSettings::kUnsetPosition;
    if (xUnset != yUnset) {
        report.adjustments.push_back(
            "window.x/window.y: only one coordinate was stored, centring instead");
        config.window.x = WindowSettings::kUnsetPosition;
        config.window.y = WindowSettings::kUnsetPosition;
    }

    return report;
}

} // namespace jarvis::config
