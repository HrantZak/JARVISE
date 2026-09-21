#include "jarvis/llm/LlamaCppBackend.h"

#include "llama.h"
#include "ggml-backend.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>
#include <string_view>
#include <thread>

#include "jarvis/logging/Logger.h"

namespace jarvis::llm {
namespace {

using namespace jarvis::core;

constexpr std::string_view kCategory = "llm";

/// llama.cpp's log slot is global, so the bridge that owns it is too. Only one
/// backend captures at a time; the mutex makes that explicit rather than
/// hopeful.
std::mutex g_captureMutex;
LlamaCppBackend* g_capturingBackend = nullptr;

/// Trims the trailing newline llama.cpp puts on every line.
std::string trimmed(const char* text) {
    if (text == nullptr) {
        return {};
    }
    std::string line{text};
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

logging::LogLevel toJarvisLevel(ggml_log_level level) {
    switch (level) {
    case GGML_LOG_LEVEL_DEBUG: return logging::LogLevel::Debug;
    case GGML_LOG_LEVEL_INFO:  return logging::LogLevel::Debug;  // llama.cpp is chatty
    case GGML_LOG_LEVEL_WARN:  return logging::LogLevel::Warning;
    case GGML_LOG_LEVEL_ERROR: return logging::LogLevel::Error;
    default:                   return logging::LogLevel::Debug;
    }
}

} // namespace

/// Free function with C linkage-compatible signature, declared a friend so it
/// can reach the backend's capture buffer.
struct LlamaLogBridge {
    static void callback(ggml_log_level level, const char* text, void* /*userData*/) {
        const std::string line = trimmed(text);
        if (line.empty()) {
            return;
        }

        logging::Logger& logger = logging::Logger::instance();
        const logging::LogLevel mapped = toJarvisLevel(level);
        if (logger.isEnabled(mapped)) {
            logger.log(mapped, "llama", line);
        }

        std::lock_guard lock{g_captureMutex};
        if (g_capturingBackend != nullptr) {
            std::lock_guard bufferLock{g_capturingBackend->m_logMutex};
            if (g_capturingBackend->m_capturing) {
                g_capturingBackend->m_capturedLog.push_back(line);
            }
        }
    }
};

namespace {

/// llama_backend_init() touches process-wide state and must run exactly once.
void ensureLlamaInitialised() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(&LlamaLogBridge::callback, nullptr);
        llama_backend_init();
        JARVIS_LOG_INFO(kCategory, "llama.cpp initialised: {}",
                        LlamaCppBackend::buildInfo());
    });
}

/// Pulls "offloaded 37/37 layers to GPU" out of the load log.
/// Returns -1 when llama.cpp did not print such a line, which is itself
/// information: it means nothing was offloaded.
std::int32_t parseOffloadedLayers(const std::vector<std::string>& log) {
    constexpr std::string_view kMarker = "offloaded ";
    constexpr std::string_view kSuffix = " layers to GPU";

    for (const std::string& line : log) {
        const std::size_t start = line.find(kMarker);
        if (start == std::string::npos || line.find(kSuffix) == std::string::npos) {
            continue;
        }

        const std::size_t numberStart = start + kMarker.size();
        const std::size_t slash = line.find('/', numberStart);
        if (slash == std::string::npos) {
            continue;
        }

        std::int32_t value = 0;
        const char* first = line.data() + numberStart;
        const char* last = line.data() + slash;
        if (std::from_chars(first, last, value).ec == std::errc{}) {
            return value;
        }
    }
    return -1;
}

std::uint32_t defaultThreadCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware > 1 ? hardware - 1 : 1;
}

} // namespace

// ---------------------------------------------------------------------------

LlamaCppBackend::LlamaCppBackend() {
    ensureLlamaInitialised();
}

LlamaCppBackend::~LlamaCppBackend() {
    unload();
}

std::string LlamaCppBackend::buildInfo() {
    return std::format("llama.cpp (Vulkan backend, {} device(s) registered)",
                       ggml_backend_dev_count());
}

void LlamaCppBackend::beginLogCapture() {
    {
        std::lock_guard lock{m_logMutex};
        m_capturedLog.clear();
        m_capturing = true;
    }
    std::lock_guard lock{g_captureMutex};
    g_capturingBackend = this;
}

void LlamaCppBackend::endLogCapture() {
    {
        std::lock_guard lock{g_captureMutex};
        if (g_capturingBackend == this) {
            g_capturingBackend = nullptr;
        }
    }
    std::lock_guard lock{m_logMutex};
    m_capturing = false;
}

std::vector<DeviceInfo> LlamaCppBackend::devices() const {
    ensureLlamaInitialised();

    std::vector<DeviceInfo> result;
    const std::size_t count = ggml_backend_dev_count();
    result.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        ggml_backend_dev_t device = ggml_backend_dev_get(i);
        if (device == nullptr) {
            continue;
        }

        DeviceInfo info;
        const char* deviceName = ggml_backend_dev_name(device);
        const char* description = ggml_backend_dev_description(device);
        info.backend = deviceName != nullptr ? deviceName : "unknown";
        info.name = description != nullptr ? description : info.backend;

        // `auto`, because ggml_backend_dev_type names both an enum and the
        // accessor function; a declaration spelled out in full parses as the
        // type and shadows the call.
        const auto deviceType = ggml_backend_dev_type(device);
        info.isGpu = (deviceType == GGML_BACKEND_DEVICE_TYPE_GPU);

        std::size_t freeBytes = 0;
        std::size_t totalBytes = 0;
        ggml_backend_dev_memory(device, &freeBytes, &totalBytes);
        info.freeMemoryBytes = freeBytes;
        info.totalMemoryBytes = totalBytes;

        result.push_back(std::move(info));
    }

    return result;
}

bool LlamaCppBackend::supportsGpuOffload() const noexcept {
    ensureLlamaInitialised();
    return llama_supports_gpu_offload();
}

core::Status LlamaCppBackend::load(const ModelInfo& model, const LoadParams& params) {
    ensureLlamaInitialised();

    if (isLoaded()) {
        unload();
    }

    if (!model.isValid()) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("'{}' is not a usable GGUF model",
                                model.path.filename().string()));
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(model.path, ec)) {
        return fail(ErrorCode::NotFound,
                    std::format("model file is gone: {}", model.path.string()));
    }

    // Context length: the model's trained window, clamped by our own ceiling.
    std::uint32_t contextLength = params.contextLength;
    if (contextLength == 0) {
        contextLength = model.contextLength;
    }
    if (params.maxContextLength > 0) {
        contextLength = std::min(contextLength, params.maxContextLength);
    }
    if (model.contextLength > 0) {
        contextLength = std::min(contextLength, model.contextLength);
    }
    if (contextLength == 0) {
        contextLength = 4096;
    }

    JARVIS_LOG_INFO(kCategory, "loading '{}' ({} layers requested on GPU, context {})",
                    model.displayName(), params.gpuLayers, contextLength);

    beginLogCapture();

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = params.gpuLayers;

    // mmap and mlock are a single enum in this revision rather than two bools.
    if (params.useMmap && params.useMlock) {
        modelParams.load_mode = LLAMA_LOAD_MODE_MMAP_MLOCK;
    } else if (params.useMmap) {
        modelParams.load_mode = LLAMA_LOAD_MODE_MMAP;
    } else if (params.useMlock) {
        modelParams.load_mode = LLAMA_LOAD_MODE_MLOCK;
    } else {
        modelParams.load_mode = LLAMA_LOAD_MODE_NONE;
    }

    const std::string pathUtf8 = model.path.string();
    m_model = llama_model_load_from_file(pathUtf8.c_str(), modelParams);

    if (m_model == nullptr) {
        endLogCapture();
        return fail(ErrorCode::Unavailable,
                    std::format("llama.cpp could not load '{}'",
                                model.path.filename().string()));
    }

    llama_context_params contextParams = llama_context_default_params();
    contextParams.n_ctx = contextLength;
    contextParams.n_batch = params.batchSize;
    contextParams.n_ubatch = std::min<std::uint32_t>(params.batchSize, 512);

    const auto threads = static_cast<std::int32_t>(
        params.threads > 0 ? params.threads : defaultThreadCount());
    contextParams.n_threads = threads;
    contextParams.n_threads_batch = threads;

    m_context = llama_init_from_model(m_model, contextParams);

    if (m_context == nullptr) {
        llama_model_free(m_model);
        m_model = nullptr;
        endLogCapture();
        return fail(ErrorCode::ResourceExhausted,
                    std::format("could not create a {}-token context for '{}'",
                                contextLength, model.displayName()));
    }

    endLogCapture();

    m_loaded = LoadedModel{};
    m_loaded.info = model;
    m_loaded.params = params;
    m_loaded.params.contextLength = contextLength;
    m_loaded.contextLength = llama_n_ctx(m_context);
    m_loaded.totalLayers = llama_model_n_layer(m_model);
    m_loaded.backendName = std::string{name()};

    {
        std::lock_guard lock{m_logMutex};
        m_loaded.loadLog = m_capturedLog;
    }

    // The authoritative offload count comes from what llama.cpp reported, not
    // from what we asked for.
    const std::int32_t offloaded = parseOffloadedLayers(m_loaded.loadLog);
    m_loaded.offloadedLayers = offloaded >= 0 ? offloaded : 0;

    for (const DeviceInfo& device : devices()) {
        if (device.isGpu) {
            m_loaded.deviceName = device.name;
            break;
        }
    }
    if (m_loaded.deviceName.empty()) {
        m_loaded.deviceName = "CPU";
    }

    JARVIS_LOG_INFO(kCategory,
                    "loaded '{}': {}/{} layers on GPU, context {}, device {}",
                    model.displayName(), m_loaded.offloadedLayers,
                    m_loaded.totalLayers, m_loaded.contextLength,
                    m_loaded.deviceName);

    if (m_loaded.offloadedLayers == 0) {
        JARVIS_LOG_WARN(kCategory,
                        "no layers were offloaded - inference will run on the CPU");
    }

    return ok();
}

void LlamaCppBackend::unload() {
    if (m_context != nullptr) {
        llama_free(m_context);
        m_context = nullptr;
    }
    if (m_model != nullptr) {
        llama_model_free(m_model);
        m_model = nullptr;
    }
    if (!m_loaded.info.path.empty()) {
        JARVIS_LOG_INFO(kCategory, "unloaded '{}'", m_loaded.info.displayName());
    }
    m_loaded = LoadedModel{};
}

core::Result<std::string> LlamaCppBackend::applyChatTemplate(
    const std::vector<ChatMessage>& messages) const {
    if (m_model == nullptr) {
        return fail(ErrorCode::Unavailable, "no model is loaded");
    }
    if (messages.empty()) {
        return fail(ErrorCode::InvalidArgument, "no messages to send");
    }

    std::vector<llama_chat_message> native;
    native.reserve(messages.size());
    for (const ChatMessage& message : messages) {
        native.push_back(llama_chat_message{
            .role = roleName(message.role).data(),
            .content = message.content.c_str(),
        });
    }

    const char* templateText = llama_model_chat_template(m_model, nullptr);
    if (templateText == nullptr) {
        JARVIS_LOG_WARN(kCategory,
                        "model has no chat template; falling back to ChatML");
    }

    // Size the buffer generously, then grow if llama.cpp asks for more.
    std::size_t capacity = 0;
    for (const ChatMessage& message : messages) {
        capacity += message.content.size() + 64;
    }
    capacity = std::max<std::size_t>(capacity, 1024);

    std::string buffer(capacity, '\0');
    std::int32_t written = llama_chat_apply_template(
        templateText, native.data(), native.size(), /*add_ass=*/true,
        buffer.data(), static_cast<std::int32_t>(buffer.size()));

    if (written > static_cast<std::int32_t>(buffer.size())) {
        buffer.resize(static_cast<std::size_t>(written));
        written = llama_chat_apply_template(
            templateText, native.data(), native.size(), true, buffer.data(),
            static_cast<std::int32_t>(buffer.size()));
    }

    if (written < 0) {
        return fail(ErrorCode::InternalFailure,
                    "the model's chat template could not be applied");
    }

    buffer.resize(static_cast<std::size_t>(written));
    return buffer;
}

core::Result<std::vector<int>> LlamaCppBackend::tokenize(const std::string& text,
                                                          bool addSpecial) const {
    const llama_vocab* vocab = llama_model_get_vocab(m_model);
    if (vocab == nullptr) {
        return fail(ErrorCode::InternalFailure, "model has no vocabulary");
    }

    // Negative return means "the buffer needed this many entries".
    const std::int32_t needed = -llama_tokenize(
        vocab, text.data(), static_cast<std::int32_t>(text.size()), nullptr, 0,
        addSpecial, /*parse_special=*/true);

    if (needed <= 0) {
        return fail(ErrorCode::ParseFailure, "the prompt tokenised to nothing");
    }

    std::vector<int> tokens(static_cast<std::size_t>(needed));
    const std::int32_t written = llama_tokenize(
        vocab, text.data(), static_cast<std::int32_t>(text.size()), tokens.data(),
        needed, addSpecial, true);

    if (written < 0) {
        return fail(ErrorCode::ParseFailure, "tokenisation failed");
    }
    tokens.resize(static_cast<std::size_t>(written));
    return tokens;
}

std::string LlamaCppBackend::tokenToText(int token) const {
    const llama_vocab* vocab = llama_model_get_vocab(m_model);
    if (vocab == nullptr) {
        return {};
    }

    std::array<char, 256> buffer{};
    const std::int32_t written = llama_token_to_piece(
        vocab, token, buffer.data(), static_cast<std::int32_t>(buffer.size()),
        /*lstrip=*/0, /*special=*/false);

    if (written <= 0) {
        return {};
    }
    return std::string{buffer.data(), static_cast<std::size_t>(written)};
}

llama_sampler* LlamaCppBackend::buildSampler(const SamplingParams& sampling) const {
    llama_sampler_chain_params chainParams = llama_sampler_chain_default_params();
    chainParams.no_perf = true;

    llama_sampler* chain = llama_sampler_chain_init(chainParams);
    if (chain == nullptr) {
        return nullptr;
    }

    const llama_vocab* vocab = llama_model_get_vocab(m_model);
    const std::int32_t vocabSize = vocab != nullptr ? llama_vocab_n_tokens(vocab) : 0;

    llama_sampler_chain_add(
        chain, llama_sampler_init_penalties(vocabSize, sampling.repeatLastN,
                                            sampling.repeatPenalty,
                                            /*penalty_freq=*/0.0F,
                                            /*penalty_present=*/0.0F));

    if (sampling.temperature <= 0.0F) {
        // Temperature 0 means deterministic: skip the truncation samplers
        // entirely rather than feeding them a degenerate distribution.
        llama_sampler_chain_add(chain, llama_sampler_init_greedy());
        return chain;
    }

    if (sampling.topK > 0) {
        llama_sampler_chain_add(chain, llama_sampler_init_top_k(sampling.topK));
    }
    if (sampling.topP < 1.0F) {
        llama_sampler_chain_add(chain, llama_sampler_init_top_p(sampling.topP, 1));
    }
    llama_sampler_chain_add(chain, llama_sampler_init_temp(sampling.temperature));

    const std::uint32_t seed = sampling.seed == SamplingParams::kSeedRandom
                                   ? LLAMA_DEFAULT_SEED
                                   : sampling.seed;
    llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));

    return chain;
}

core::Result<GenerationStats> LlamaCppBackend::generate(
    const GenerationRequest& request, const TokenCallback& onToken) {
    using Clock = std::chrono::steady_clock;

    if (!isLoaded()) {
        return fail(ErrorCode::Unavailable,
                    "no model is loaded; load one before generating");
    }

    m_stopRequested.store(false);

    core::Result<std::string> prompt = applyChatTemplate(request.messages);
    if (!prompt) {
        return std::unexpected(prompt.error());
    }

    core::Result<std::vector<int>> tokensResult = tokenize(*prompt, /*addSpecial=*/true);
    if (!tokensResult) {
        return std::unexpected(tokensResult.error());
    }
    std::vector<int> promptTokens = std::move(*tokensResult);

    const auto contextLength = static_cast<std::size_t>(m_loaded.contextLength);
    if (promptTokens.size() >= contextLength) {
        return fail(ErrorCode::ResourceExhausted,
                    std::format("the conversation needs {} tokens but the context "
                                "holds only {}",
                                promptTokens.size(), contextLength));
    }

    // Each call replays the whole conversation, so the cache starts clean.
    llama_memory_clear(llama_get_memory(m_context), /*data=*/true);

    GenerationStats stats;
    stats.promptTokens = static_cast<std::int32_t>(promptTokens.size());

    // --- prompt ---------------------------------------------------------
    const auto promptStart = Clock::now();

    const auto batchSize = static_cast<std::size_t>(
        std::max<std::uint32_t>(m_loaded.params.batchSize, 1));

    for (std::size_t offset = 0; offset < promptTokens.size(); offset += batchSize) {
        const std::size_t count = std::min(batchSize, promptTokens.size() - offset);
        llama_batch batch = llama_batch_get_one(promptTokens.data() + offset,
                                                static_cast<std::int32_t>(count));
        if (llama_decode(m_context, batch) != 0) {
            return fail(ErrorCode::InternalFailure,
                        "llama_decode failed while processing the prompt");
        }
        if (m_stopRequested.load()) {
            stats.cancelled = true;
            return stats;
        }
    }

    stats.promptMs =
        std::chrono::duration<double, std::milli>(Clock::now() - promptStart).count();

    // --- generation -----------------------------------------------------
    llama_sampler* sampler = buildSampler(request.sampling);
    if (sampler == nullptr) {
        return fail(ErrorCode::InternalFailure, "could not build the sampler chain");
    }

    struct SamplerGuard {
        llama_sampler* chain;
        ~SamplerGuard() { llama_sampler_free(chain); }
    } guard{sampler};

    const llama_vocab* vocab = llama_model_get_vocab(m_model);
    const std::int32_t budget =
        request.sampling.maxTokens > 0 ? request.sampling.maxTokens
                                       : std::numeric_limits<std::int32_t>::max();

    const auto generateStart = Clock::now();

    for (std::int32_t produced = 0; produced < budget; ++produced) {
        if (m_stopRequested.load()) {
            stats.cancelled = true;
            break;
        }

        llama_token token = llama_sampler_sample(sampler, m_context, -1);
        if (llama_vocab_is_eog(vocab, token)) {
            break;
        }

        llama_sampler_accept(sampler, token);
        ++stats.generatedTokens;

        const std::string piece = tokenToText(token);
        if (!piece.empty() && !onToken(piece)) {
            stats.cancelled = true;
            break;
        }

        // Stop before overrunning the context rather than after.
        if (static_cast<std::size_t>(stats.promptTokens + stats.generatedTokens) >=
            contextLength) {
            stats.truncated = true;
            break;
        }

        llama_batch batch = llama_batch_get_one(&token, 1);
        if (llama_decode(m_context, batch) != 0) {
            return fail(ErrorCode::InternalFailure,
                        "llama_decode failed while generating");
        }
    }

    if (stats.generatedTokens >= budget) {
        stats.truncated = true;
    }

    stats.generateMs =
        std::chrono::duration<double, std::milli>(Clock::now() - generateStart).count();

    JARVIS_LOG_INFO(kCategory,
                    "generated {} tokens in {:.0f} ms ({:.1f} tok/s), prompt {} tokens "
                    "({:.1f} tok/s){}",
                    stats.generatedTokens, stats.generateMs,
                    stats.generatedTokensPerSecond(), stats.promptTokens,
                    stats.promptTokensPerSecond(),
                    stats.cancelled ? " [cancelled]" : "");

    return stats;
}

} // namespace jarvis::llm
