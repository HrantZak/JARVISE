#include "jarvis/voice/VoiceTypes.h"

namespace jarvis::voice {

std::string_view pipelineStateKey(VoicePipelineState state) noexcept {
    // Untranslated identifiers: the UI branches on these and the log records
    // them, exactly like AiCoreModel::stateKey.
    switch (state) {
    case VoicePipelineState::Disabled:     return "DISABLED";
    case VoicePipelineState::Unavailable:  return "UNAVAILABLE";
    case VoicePipelineState::Idle:         return "IDLE";
    case VoicePipelineState::Listening:    return "LISTENING";
    case VoicePipelineState::Transcribing: return "TRANSCRIBING";
    case VoicePipelineState::Thinking:     return "THINKING";
    case VoicePipelineState::Synthesizing: return "SYNTHESIZING";
    case VoicePipelineState::Speaking:     return "SPEAKING";
    case VoicePipelineState::Error:        return "ERROR";
    }
    return "UNAVAILABLE";
}

} // namespace jarvis::voice
