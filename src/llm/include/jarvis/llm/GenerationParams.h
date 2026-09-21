#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jarvis::llm {

/// How a model is brought into memory.
struct LoadParams {
    /// Layers to offload to the GPU. -1 means "all of them"; 0 is CPU-only.
    int gpuLayers{-1};

    /// Context window in tokens. 0 takes the model's trained length, clamped by
    /// maxContextLength below.
    std::uint32_t contextLength{8192};

    /// Generation threads. 0 picks hardware_concurrency() - 1.
    std::uint32_t threads{0};

    /// Prompt-processing batch size.
    std::uint32_t batchSize{512};

    /// Refuse to allocate a context larger than this even if the model allows
    /// it. Qwen3 advertises 40960 tokens, whose KV cache alone would not fit in
    /// 8 GB of VRAM.
    std::uint32_t maxContextLength{32768};

    /// Lock the model in RAM. Off by default: it interacts badly with an 8 GB
    /// card plus a desktop session.
    bool useMlock{false};

    /// Memory-map the file. On by default - it is what makes a 5 GB model load
    /// in seconds on a warm cache.
    bool useMmap{true};
};

/// How tokens are sampled.
struct SamplingParams {
    float temperature{0.7F};
    float topP{0.9F};
    std::int32_t topK{40};
    float repeatPenalty{1.05F};
    std::int32_t repeatLastN{64};

    /// Hard cap on generated tokens. 0 means "until end of turn".
    std::int32_t maxTokens{512};

    /// Fixed seed for reproducibility; kSeedRandom draws one.
    std::uint32_t seed{kSeedRandom};

    static constexpr std::uint32_t kSeedRandom = 0xFFFFFFFFU;
};

/// One turn in a conversation.
struct ChatMessage {
    enum class Role { System, User, Assistant };

    Role role{Role::User};
    std::string content;
};

[[nodiscard]] std::string_view roleName(ChatMessage::Role role) noexcept;

/// A request to continue a conversation.
struct GenerationRequest {
    bool reasoning{false};
    std::vector<ChatMessage> messages;
    SamplingParams sampling;
};

/// What a completed generation cost.
struct GenerationStats {
    std::int32_t promptTokens{0};
    std::int32_t generatedTokens{0};
    double promptMs{0.0};
    double generateMs{0.0};

    /// Whether generation ended because the caller asked it to stop.
    bool cancelled{false};

    /// Whether generation ended because maxTokens was reached rather than the
    /// model finishing its turn.
    bool truncated{false};

    [[nodiscard]] double promptTokensPerSecond() const noexcept {
        return promptMs > 0.0 ? promptTokens * 1000.0 / promptMs : 0.0;
    }

    [[nodiscard]] double generatedTokensPerSecond() const noexcept {
        return generateMs > 0.0 ? generatedTokens * 1000.0 / generateMs : 0.0;
    }
};

} // namespace jarvis::llm
