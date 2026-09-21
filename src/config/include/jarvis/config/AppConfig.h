#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "jarvis/i18n/Language.h"
#include "jarvis/logging/LogLevel.h"

namespace jarvis::config {

/// Schema version of the on-disk config file. Bump when a field changes
/// meaning so ConfigStore can migrate instead of silently misreading.
///
/// Version 2 added general.language.
/// Version 3 added the llm section.
/// Version 4 added the voice section.
/// Version 5 added the tools section.
/// Version 6 added the agent section.
inline constexpr int kCurrentConfigVersion = 6;

struct GeneralSettings {
    /// Interface and reply language. Russian is the default: JARVIS is a
    /// Russian-first assistant, not an English one with a translation.
    i18n::Language language{i18n::kDefaultLanguage};
};

struct LoggingSettings {
    logging::LogLevel level{logging::LogLevel::Info};
    bool logToConsole{false};
    int retentionDays{14};
    std::size_t maxFileBytes{8u * 1024u * 1024u};
};

struct WindowSettings {
    /// Sentinel for "no stored position; let the system centre the window".
    static constexpr int kUnsetPosition = -1;

    int width{1280};
    int height{800};
    int x{kUnsetPosition};
    int y{kUnsetPosition};
    bool rememberGeometry{true};
};

/// The complete persisted configuration.
///
/// Phase 1 only declares settings that are actually honoured by the running
/// application. Sections for AI, voice, permissions and so on are added in the
/// phase that implements them, so the file never advertises a knob that does
/// nothing.
/// Local model settings. CPU-only is the safe default: GPU offload remains an
/// explicit configuration choice and is never required to run JARVIS.
struct LlmSettings {
    std::string responseMode{"balanced"};
    bool jarvisPersonality{true};
    /// Absolute path of the model to load at start-up. Empty means "none":
    /// JARVIS never picks a model on the user's behalf.
    std::string modelPath;

    /// Load the configured model when the application starts.
    bool autoLoad{false};

    /// Layers to offload. -1 is all of them, 0 is CPU-only.
    int gpuLayers{0};

    std::uint32_t contextLength{8192};
    std::uint32_t threads{0};      ///< 0 selects hardware_concurrency() - 1
    std::uint32_t batchSize{512};

    float temperature{0.7F};
    float topP{0.9F};
    std::int32_t topK{40};
    std::int32_t maxTokens{512};

    /// Also offer models found in Ollama's blob store.
    bool useOllamaModels{true};

    /// Ask reasoning models not to think out loud before answering.
    ///
    /// Qwen3 reasons inside a `<think>` block and JARVIS throws that block
    /// away - the interface never shows it. Every one of those tokens is
    /// generated at the same speed as a visible one and then discarded, so on
    /// an ordinary question the reasoning is most of the wait for nothing. The
    /// `/no_think` hint switches it off.
    ///
    /// On by default because this assistant answers questions about the machine
    /// and runs short commands, where reasoning buys little. Turn it off for
    /// work where the model needs to think first.
    bool suppressReasoning{true};

    /// Extra folders to scan for .gguf files, searched before the defaults.
    ///
    /// Empty by default: JARVIS looks in its own model directory and in
    /// Ollama's blob store, and goes rummaging through a person's folders only
    /// when told to. Put a Downloads folder here and a model left there is
    /// offered without being moved.
    std::vector<std::string> modelDirectories;
};

/// Speech in and speech out.
///
/// Paths are configuration, never model output: the TTS backend runs a child
/// process, and the executable it runs is fixed here at start-up.
struct VoiceSettings {
    int sttThreads{4};
    /// Master switch. Off means the microphone is never opened.
    bool enabled{false};

    /// Start listening as soon as a model and the voice backends are ready.
    bool autoListen{false};

    /// Device ids from the platform. Empty means "system default", which is
    /// what survives a headset being unplugged.
    std::string inputDeviceId;
    std::string outputDeviceId;

    // --- Speech to text ---------------------------------------------------

    /// GGML model for whisper.cpp. Relative paths resolve against the
    /// application root.
    std::string sttModelPath{"models/whisper/ggml-large-v3-turbo.bin"};

    /// Offload Whisper to the GPU.
    ///
    /// **Off by default, and that is a measurement, not caution.** On an 8 GB
    /// card, Whisper large-v3-turbo (1.6 GB) plus Qwen3 8B Q4_K_M (5.5 GB) plus
    /// the desktop does not fit, and the driver starts evicting. Measured on the
    /// reference machine, same prompt, both models resident:
    ///
    ///     Whisper on GPU : STT 8768 ms, LLM 7.0 tok/s,  round trip 28.2 s
    ///     Whisper on CPU : STT 4599 ms, LLM 42.4 tok/s, round trip 11.3 s
    ///
    /// Moving Whisper off the GPU makes the language model six times faster and
    /// Whisper itself twice as fast, because neither is thrashing any more.
    /// Turn this on only with a smaller language model or a larger card.
    bool sttUseGpu{false};

    // --- Text to speech ---------------------------------------------------

    std::string ttsExecutable;
    std::string ttsRuVoice;
    std::string ttsEnVoice;

    /// Speaking rate; larger is slower. Piper calls this length_scale.
    float ttsLengthScale{1.0F};

    /// Playback volume, 0..1.
    float ttsVolume{1.0F};

    // --- Voice activity detection ----------------------------------------

    float vadActivationThreshold{0.015F};
    float vadReleaseThreshold{0.008F};
    int vadSilenceTimeoutMs{800};
    int vadMinimumSpeechMs{300};
    int vadMaximumUtteranceMs{30000};
    int vadPreRollMs{300};
};

/// What the assistant is allowed to do on this machine.
///
/// Every default here is the safe one. Nothing in this struct can enable a
/// DENIED tool - that level is not a setting, it is a property of the tool - and
/// nothing here can make a CONFIRM_REQUIRED action run unattended. The most a
/// user can do by editing this file is *narrow* what is permitted.
struct ToolSettings {
    /// Master switch. Off means no tool runs at all, whatever else is set.
    bool enabled{true};

    /// Tools that only read state. On by default: they cannot change anything,
    /// and without them the assistant has to guess at facts it could look up.
    bool allowReadOnly{true};

    /// Reversible actions that run without asking.
    bool allowSafeActions{true};

    /// Actions that ask first. Turning this off refuses them outright rather
    /// than prompting; it never turns them into silent actions.
    bool allowConfirmedActions{true};

    /// How many tool rounds one user turn may take before the loop stops. A
    /// model that keeps calling tools instead of answering is bounded here, not
    /// by asking it nicely to stop.
    int maxRounds{4};

    /// How long a confirmation dialog stays valid. After this the request
    /// lapses and nothing runs.
    int confirmationTimeoutMs{60000};

    /// Record every call and every refusal.
    bool auditEnabled{true};

    /// How many audit records to keep in memory.
    int auditCapacity{2000};
};

/// The task-oriented agent.
///
/// Every limit here has a hard ceiling in the code that owns it - the plan size
/// in Task, the tool-result size in ContextManager, the retry count below.
/// Configuration may lower a limit; it can never raise one past its ceiling, so
/// no setting can widen the agent's reach.
struct AgentSettings {
    /// Master switch. Off leaves the assistant answering one message at a time
    /// exactly as it did before the agent existed.
    bool enabled{true};

    /// Let the model propose a multi-step plan.
    ///
    /// **Off by default, and that is a measurement.** Adding the plan format to
    /// the system prompt takes it from 572 to 759 tokens on Qwen3 8B, and the
    /// larger, planning-flavoured prompt measurably changes what the model does
    /// with ordinary questions: in a live run it answered "how much memory do I
    /// have" with cpu_info, and reached for time_info to answer "what is two
    /// plus two". Multi-step planning is implemented and tested; it is opt-in
    /// until the prompt is tuned so that turning it on does not degrade the
    /// single-step path.
    bool plannerEnabled{false};

    /// Steps one task may hold. Ceiling: agent::Task::kMaxSteps (32).
    int maxSteps{8};

    /// Attempts per step, including the first. Ceiling: kMaxRetryCeiling.
    int maxRetries{2};

    /// Tool executions one task may perform, across all steps and retries.
    int maxToolCalls{16};

    /// How long one task may run before it is stopped, in milliseconds.
    int maxRuntimeMs{300000};

    /// Attempt a bounded recovery when a step fails for a transient reason.
    bool recoveryEnabled{true};

    /// Largest tool result kept, in characters. Ceiling:
    /// agent::ContextManager::kMaxToolResultChars (4096).
    int maxToolResultChars{4096};

    /// Remember anything between tasks at all.
    bool memoryEnabled{false};

    /// Keep memory across restarts. **Off by default**: anything written here
    /// becomes input to future prompts, so it is opt-in.
    bool memoryPersistent{false};

    int maxMemoryEntries{64};
    int maxMemoryEntryChars{512};
};

/// The highest retry count the code will honour, whatever the config says.
inline constexpr int kMaxRetryCeiling = 5;

struct AppConfig {
    int configVersion{kCurrentConfigVersion};
    GeneralSettings general;
    LoggingSettings logging;
    WindowSettings window;
    LlmSettings llm;
    VoiceSettings voice;
    ToolSettings tools;
    AgentSettings agent;
};

/// Outcome of checking a config against its allowed ranges. Out-of-range values
/// are clamped rather than rejected, and every adjustment is reported so it can
/// be logged and shown to the user.
struct ValidationReport {
    std::vector<std::string> adjustments;

    [[nodiscard]] bool clean() const noexcept { return adjustments.empty(); }
};

/// Clamps out-of-range fields in place and describes what was changed.
[[nodiscard]] ValidationReport validate(AppConfig& config);

} // namespace jarvis::config
