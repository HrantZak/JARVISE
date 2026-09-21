#include "LlmController.h"

#include <filesystem>
#include "ResponsePolicy.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QVariantMap>

#include <utility>

#include "AiCoreModel.h"
#include "ToolCoordinator.h"
#include "jarvis/config/AppPaths.h"
#include "jarvis/llm/GgufInspector.h"
#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "llm";

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString formatBytes(std::uint64_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / kGiB, 0, 'f', 2);
}

} // namespace

// ---------------------------------------------------------------------------
// ConversationModel
// ---------------------------------------------------------------------------

ConversationModel::ConversationModel(QObject* parent)
    : QAbstractListModel{parent} {}

int ConversationModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant ConversationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }

    const Entry& entry = m_entries[static_cast<std::size_t>(index.row())];
    switch (role) {
    case RoleRole:      return entry.role;
    case TextRole:      return entry.text;
    case TimestampRole: return entry.timestamp;
    case StreamingRole: return entry.streaming;
    default:            return {};
    }
}

QHash<int, QByteArray> ConversationModel::roleNames() const {
    return {
        {RoleRole, "role"},
        {TextRole, "text"},
        {TimestampRole, "timestamp"},
        {StreamingRole, "streaming"},
    };
}

void ConversationModel::appendUser(const QString& text) {
    const int row = static_cast<int>(m_entries.size());
    beginInsertRows({}, row, row);
    m_entries.push_back(Entry{
        .role = QStringLiteral("user"),
        .text = text,
        .timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")),
        .streaming = false,
    });
    endInsertRows();
    Q_EMIT countChanged();
}

void ConversationModel::beginAssistant() {
    const int row = static_cast<int>(m_entries.size());
    beginInsertRows({}, row, row);
    m_entries.push_back(Entry{
        .role = QStringLiteral("assistant"),
        .text = {},
        .timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")),
        .streaming = true,
    });
    endInsertRows();
    Q_EMIT countChanged();
}

void ConversationModel::appendToAssistant(const QString& piece) {
    if (m_entries.empty()) {
        return;
    }
    const int row = static_cast<int>(m_entries.size()) - 1;
    m_entries.back().text += piece;

    const QModelIndex changed = index(row, 0);
    Q_EMIT dataChanged(changed, changed, {TextRole});
}

void ConversationModel::replaceAssistant(const QString& text) {
    if (m_entries.empty()) {
        return;
    }
    const int row = static_cast<int>(m_entries.size()) - 1;
    m_entries.back().text = text;

    const QModelIndex changed = index(row, 0);
    Q_EMIT dataChanged(changed, changed, {TextRole});
}

void ConversationModel::endAssistant() {
    if (m_entries.empty()) {
        return;
    }
    const int row = static_cast<int>(m_entries.size()) - 1;
    m_entries.back().streaming = false;

    const QModelIndex changed = index(row, 0);
    Q_EMIT dataChanged(changed, changed, {StreamingRole});
}

void ConversationModel::clear() {
    if (m_entries.empty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
    Q_EMIT countChanged();
}

// ---------------------------------------------------------------------------
// ThinkFilter
// ---------------------------------------------------------------------------

QString LlmController::Utf8Assembler::feed(std::string_view piece) {
    m_pending.append(piece);

    // Walk back from the end over continuation bytes (10xxxxxx) to find the
    // start of the last character. If that character is incomplete, hold it
    // back until the next piece brings the rest.
    std::size_t complete = m_pending.size();
    std::size_t back = 0;
    while (complete > 0 && back < 4) {
        const auto byte = static_cast<unsigned char>(m_pending[complete - 1]);
        if ((byte & 0xC0U) == 0x80U) {
            --complete;
            ++back;
            continue;
        }

        // A lead byte. Work out how long its character should be.
        std::size_t needed = 1;
        if ((byte & 0xE0U) == 0xC0U) {
            needed = 2;
        } else if ((byte & 0xF0U) == 0xE0U) {
            needed = 3;
        } else if ((byte & 0xF8U) == 0xF0U) {
            needed = 4;
        }

        if (m_pending.size() - (complete - 1) < needed) {
            --complete;  // the character is not all here yet
        } else {
            complete = m_pending.size();
        }
        break;
    }

    const QString text = QString::fromUtf8(m_pending.data(),
                                           static_cast<qsizetype>(complete));
    m_pending.erase(0, complete);
    return text;
}

QString LlmController::Utf8Assembler::flush() {
    // Whatever is left is an incomplete character with no more bytes coming.
    // Dropping it is right: showing half a character is worse than showing
    // nothing, and a well-behaved backend leaves nothing here.
    m_pending.clear();
    return {};
}

void LlmController::Utf8Assembler::reset() {
    m_pending.clear();
}

QString LlmController::ThinkFilter::feed(QStringView piece) {
    static const QString kOpen = QStringLiteral("<think>");
    static const QString kClose = QStringLiteral("</think>");

    m_pending += piece.toString();
    QString output;

    for (;;) {
        if (m_inThink) {
            const qsizetype close = m_pending.indexOf(kClose);
            if (close < 0) {
                // Keep only enough to recognise a marker split across tokens.
                if (m_pending.size() > kClose.size()) {
                    m_pending = m_pending.right(kClose.size());
                }
                break;
            }
            m_pending.remove(0, close + kClose.size());
            m_inThink = false;
            continue;
        }

        const qsizetype open = m_pending.indexOf(kOpen);
        if (open < 0) {
            // Hold back a possible partial "<think" at the tail.
            const qsizetype keep = std::min<qsizetype>(m_pending.size(), kOpen.size() - 1);
            output += m_pending.left(m_pending.size() - keep);
            m_pending = m_pending.right(keep);
            break;
        }

        output += m_pending.left(open);
        m_pending.remove(0, open + kOpen.size());
        m_inThink = true;
    }

    return output;
}

QString LlmController::ThinkFilter::flush() {
    // feed() holds back up to six characters in case they are the start of a
    // "<think>" marker split across tokens. At the end of a generation there
    // are no more tokens coming, so whatever is held back is ordinary text and
    // has to be released - otherwise every answer loses its last six
    // characters. On prose that reads as a clipped word; on a tool call it
    // removes the closing braces and the call stops parsing entirely.
    //
    // Text still inside an unterminated think block is discarded, which is what
    // it was going to be either way.
    if (m_inThink) {
        m_pending.clear();
        return {};
    }
    return std::exchange(m_pending, QString{});
}

void LlmController::ThinkFilter::reset() {
    m_pending.clear();
    m_inThink = false;
}

// ---------------------------------------------------------------------------
// LlmController
// ---------------------------------------------------------------------------

LlmController::LlmController(core::ThreadPool& pool,
                             std::unique_ptr<llm::ILLMBackend> backend,
                             AiCoreModel& core,
                             QObject* parent)
    : QObject{parent}
    , m_pool{pool}
    , m_backend{std::move(backend)}
    , m_core{core}
    , m_registry{llm::ModelRegistry::Options{}} {
    refreshDevices();
    updateCoreState();
}

LlmController::~LlmController() {
    // A generation may still hold `this` on a worker thread.
    if (m_backend) {
        m_backend->requestStop();
    }
    m_pool.waitIdle();

    // Then give the model back. Several gigabytes of VRAM and host memory are
    // held while a model is loaded, and waiting for process teardown to reclaim
    // it leaves the card occupied for as long as the driver takes. Unloading
    // here means closing JARVIS frees the card at the moment the window closes.
    //
    // After waitIdle(), so nothing is still generating against it.
    if (m_backend && m_backend->isLoaded()) {
        m_backend->unload();
    }
}

QString LlmController::backendName() const {
    return m_backend ? toQString(m_backend->name()) : QStringLiteral("none");
}

bool LlmController::replaceBackend(std::unique_ptr<llm::ILLMBackend> backend) {
    if (m_loading || m_generating) return false;
    m_backend = std::move(backend);
    clearConversation();
    m_lastError.clear();
    Q_EMIT lastErrorChanged();
    refreshDevices();
    Q_EMIT loadedChanged();
    updateCoreState();
    return true;
}

bool LlmController::loaded() const {
    return m_backend && m_backend->isLoaded();
}

QString LlmController::loadedModelName() const {
    if (!loaded()) {
        return {};
    }
    return QString::fromStdString(m_backend->loadedModel().info.displayName());
}

QString LlmController::loadedModelPath() const {
    if (!loaded()) {
        return {};
    }
    return QString::fromStdWString(m_backend->loadedModel().info.path.wstring());
}

int LlmController::offloadedLayers() const {
    return loaded() ? m_backend->loadedModel().offloadedLayers : 0;
}

int LlmController::totalLayers() const {
    return loaded() ? m_backend->loadedModel().totalLayers : 0;
}

int LlmController::contextLength() const {
    return loaded() ? static_cast<int>(m_backend->loadedModel().contextLength) : 0;
}

bool LlmController::gpuAccelerated() const {
    return loaded() && m_backend->loadedModel().isGpuAccelerated();
}

void LlmController::applySettings(const config::LlmSettings& settings) {
    m_settings = settings;

    llm::ModelRegistry::Options options;
    options.includeOllamaBlobs = settings.useOllamaModels;

    // JARVIS's own model directory, which docs/BUILD.md documents as the place
    // to put a model and which nothing was scanning: the registry only ever
    // looked at the Ollama blob store, so a file dropped where the
    // documentation says to drop it was invisible.
    //
    // Through AppPaths rather than QStandardPaths. The first attempt used
    // AppLocalDataLocation and scanned an empty directory: the organisation and
    // the application are both called JARVIS, so Qt nests them and returns
    // %LOCALAPPDATA%\JARVIS\JARVIS. AppPaths is what everything else in the
    // application resolves, and what the documentation describes.
    if (const auto paths = config::AppPaths::resolve()) {
        options.directories.push_back(paths->modelDirectory());
    }

    // Anything the user configured explicitly comes first in the search order,
    // since a stated preference outranks a default.
    for (const std::string& directory : settings.modelDirectories) {
        if (!directory.empty()) {
            options.directories.emplace(options.directories.begin(),
                                        std::filesystem::path{directory});
        }
    }

    m_registry = llm::ModelRegistry{options};

    // The context budget follows the window the model is actually loaded with,
    // and reserves room for the answer. Without the reserve a prompt could fill
    // the window exactly and leave nothing to reply with.
    agent::ContextManager::Budget budget;
    budget.contextTokens = settings.contextLength > 0 ? settings.contextLength : 4096;
    budget.outputReserveTokens =
        static_cast<std::size_t>(std::max(settings.maxTokens, 256)) + 256;
    m_context.setBudget(budget);
}

void LlmController::setLanguage(i18n::Language language) {
    m_language = language;
}

void LlmController::refreshDevices() {
    m_devices.clear();
    m_deviceName.clear();
    m_gpuAvailable = false;

    if (!m_backend) {
        Q_EMIT devicesChanged();
        return;
    }

    for (const llm::DeviceInfo& device : m_backend->devices()) {
        QVariantMap entry;
        entry[QStringLiteral("name")] = toQString(device.name);
        entry[QStringLiteral("backend")] = toQString(device.backend);
        entry[QStringLiteral("isGpu")] = device.isGpu;
        entry[QStringLiteral("totalMemory")] = formatBytes(device.totalMemoryBytes);
        entry[QStringLiteral("freeMemory")] = formatBytes(device.freeMemoryBytes);
        m_devices.append(entry);

        if (device.isGpu && !m_gpuAvailable) {
            m_gpuAvailable = true;
            m_deviceName = toQString(device.name);
        }
    }

    if (m_deviceName.isEmpty()) {
        m_deviceName = tr("CPU only");
    }

    Q_EMIT devicesChanged();
}

void LlmController::setError(const QString& message) {
    m_lastError = message;
    JARVIS_LOG_WARN(kCategory, "{}", message.toStdString());
    Q_EMIT lastErrorChanged();
    m_core.report(AiCoreModel::Source::Llm, AiCoreModel::State::Error);
}

void LlmController::setGenerating(bool generating) {
    if (m_generating == generating) {
        return;
    }
    m_generating = generating;
    Q_EMIT generatingChanged();
    updateCoreState();
}

void LlmController::updateCoreState() {
    // Reports what the engine is doing. What the interface shows is the Core's
    // decision, made from every subsystem's report together - this one does not
    // get to overwrite the microphone or a running tool.
    if (m_generating) {
        m_core.report(AiCoreModel::Source::Llm, AiCoreModel::State::Thinking);
    } else if (m_loading) {
        m_core.report(AiCoreModel::Source::Llm, AiCoreModel::State::Executing);
    } else if (loaded()) {
        m_core.report(AiCoreModel::Source::Llm, AiCoreModel::State::Idle);
    } else {
        m_core.report(AiCoreModel::Source::Llm, AiCoreModel::State::Offline);
    }
}

void LlmController::refreshModels() {
    if (m_scanning) {
        return;
    }
    m_scanning = true;
    Q_EMIT scanningChanged();

    m_pool.post([this] {
        std::vector<llm::ModelInfo> found = m_registry.scan();
        const std::vector<std::string> warnings = m_registry.warnings();

        QMetaObject::invokeMethod(
            this,
            [this, found = std::move(found), warnings] {
                m_models.clear();
                for (const llm::ModelInfo& model : found) {
                    QVariantMap entry;
                    entry[QStringLiteral("name")] =
                        QString::fromStdString(model.displayName());
                    entry[QStringLiteral("path")] =
                        QString::fromStdWString(model.path.wstring());
                    entry[QStringLiteral("architecture")] =
                        toQString(model.architecture);
                    entry[QStringLiteral("quantization")] =
                        toQString(model.quantization);
                    entry[QStringLiteral("sizeLabel")] = toQString(model.sizeLabel);
                    entry[QStringLiteral("fileSize")] = formatBytes(model.fileSizeBytes);
                    entry[QStringLiteral("contextLength")] =
                        static_cast<int>(model.contextLength);
                    entry[QStringLiteral("blockCount")] =
                        static_cast<int>(model.blockCount);
                    entry[QStringLiteral("estimatedVram")] =
                        formatBytes(model.estimateVramBytes(m_settings.contextLength));
                    m_models.append(entry);
                }

                for (const std::string& warning : warnings) {
                    JARVIS_LOG_WARN(kCategory, "model scan: {}", warning);
                }

                JARVIS_LOG_INFO(kCategory, "model scan found {} model(s)",
                                m_models.size());

                m_scanning = false;
                Q_EMIT scanningChanged();
                Q_EMIT modelsChanged();
            },
            Qt::QueuedConnection);
    });
}

void LlmController::loadModel(const QString& path) {
    if (!m_backend) {
        setError(tr("The local model engine is not available in this build."));
        return;
    }
    if (m_loading || m_generating) {
        return;
    }

    const std::filesystem::path modelPath{path.toStdWString()};

    m_loading = true;
    Q_EMIT loadedChanged();
    updateCoreState();

    llm::LoadParams params;
    params.gpuLayers = m_settings.gpuLayers;
    params.contextLength = m_settings.contextLength;
    params.threads = m_settings.threads;
    params.batchSize = m_settings.batchSize;

    m_pool.post([this, modelPath, params] {
        core::Result<llm::ModelInfo> info = llm::GgufInspector::inspect(modelPath);

        core::Status status =
            info ? m_backend->load(*info, params)
                 : core::Status{std::unexpected(info.error())};

        QMetaObject::invokeMethod(
            this,
            [this, status = std::move(status)] {
                m_loading = false;

                if (!status) {
                    Q_EMIT loadedChanged();
                    setError(QString::fromStdString(status.error().toUserString()));
                    updateCoreState();
                    return;
                }

                m_lastError.clear();
                Q_EMIT lastErrorChanged();
                Q_EMIT loadedChanged();
                updateCoreState();

                const llm::LoadedModel& model = m_backend->loadedModel();
                JARVIS_LOG_INFO(kCategory, "model ready: {} ({}/{} layers on {})",
                                model.info.displayName(), model.offloadedLayers,
                                model.totalLayers, model.deviceName);

                Q_EMIT modelLoaded(loadedModelName());
            },
            Qt::QueuedConnection);
    });
}

void LlmController::unloadModel() {
    if (!m_backend || m_generating) {
        return;
    }
    m_backend->unload();
    m_context.clearConversation();
    Q_EMIT loadedChanged();
    updateCoreState();
}

void LlmController::clearConversation() {
    if (m_generating) {
        return;
    }
    m_context.clearConversation();
    m_conversation.clear();
}

void LlmController::cancel() {
    if (m_backend && m_generating) {
        JARVIS_LOG_INFO(kCategory, "generation cancelled by the user");
        m_backend->requestStop();
    }
}

void LlmController::send(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    // The interface's entry point, and nothing more. What happens to a request
    // - whether it needs a tool, a plan, several steps - is the agent's
    // decision, not this class's. Before Phase 6 the loop lived here, which
    // meant the component that talks to the model also decided when to run
    // tools; those two jobs pull apart as soon as a task has more than a step.
    Q_EMIT userMessageSubmitted(trimmed);
}

void LlmController::generate(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!m_backend || !loaded()) {
        setError(tr("Load a model before sending a message."));
        Q_EMIT generationCompleted({}, false);
        return;
    }
    if (m_generating) {
        // The previous generation is still winding down - typically because it
        // was cancelled a moment ago and the backend has not yet returned.
        //
        // Reported rather than ignored. Returning silently left the caller
        // holding a task that would never start and never end: the agent waited
        // for a completion nobody was going to send, and the interface showed a
        // task doing nothing. A refusal the user can see is the whole point.
        setError(tr("The previous request is still finishing. Try again in a moment."));
        Q_EMIT generationCompleted({}, false);
        return;
    }

    m_conversation.appendUser(trimmed);
    m_latestUserRequest = trimmed;
    m_context.addUser(trimmed.toStdString());
    startGeneration();
}

void LlmController::regenerate() {
    if (!m_backend || !loaded()) {
        Q_EMIT generationCompleted({}, false);
        return;
    }
    if (m_generating) {
        setError(tr("The previous request is still finishing. Try again in a moment."));
        Q_EMIT generationCompleted({}, false);
        return;
    }

    // Another turn on the context as it stands - after a tool result was added,
    // for instance. No new user message: the request has not changed.
    //
    // The assistant turn and the generating flag are opened by startGeneration
    // and nowhere else. Doing it here as well opened two turns for every tool
    // round: one empty and one streamed into, so the transcript grew a blank
    // bubble each time and the answer was read from whichever happened to be
    // last. One user turn is one generation transaction, with one owner.
    startGeneration();
}

void LlmController::appendToolResult(const QString& resultText) {
    // Framed and size-capped by the context manager. It enters as data, never
    // as a system message: that is the one role whose text carries authority.
    if (m_context.addToolResult(agent::frameToolResult(resultText.toStdString()))) {
        JARVIS_LOG_WARN(kCategory, "a tool result was truncated to fit the context");
    }
}

void LlmController::appendNote(const QString& text) {
    m_context.addUser(text.toStdString(), agent::ContextPriority::High);
}

void LlmController::replaceVisibleAnswer(const QString& text) {
    m_conversation.replaceAssistant(text);
}
void LlmController::recordLocalExchange(const QString& request, const QString& answer) {
    m_conversation.appendUser(request);
    m_conversation.beginAssistant();
    m_conversation.replaceAssistant(answer);
    m_conversation.endAssistant();
    m_context.addUser(request.toStdString(), agent::ContextPriority::High);
    m_context.addAssistant(answer.toStdString());
}

void LlmController::setPromptSection(const QString& section) {
    // What the agent wants the model told about tools and plans. Documentation
    // for the model; the boundary is elsewhere and applies regardless of it.
    m_promptSection = section;
}

void LlmController::startGeneration() {
    if (!m_backend || !loaded()) {
        return;
    }

    // The system prompt carries the language rules from LanguagePolicy, so the
    // assistant answers in the interface language by default.
    llm::GenerationRequest request;
    const i18n::LanguagePolicy policy{m_language};

    const bool cloud = m_backend->name() == "DeepSeek API";
    request.reasoning = cloud && needsReasoning(m_latestUserRequest, m_settings.responseMode);
    std::string systemPrompt = cloud ? assistantStyle(m_settings.jarvisPersonality)
        : "You are JARVIS, a desktop assistant. Answer briefly, calmly and precisely.\n\n";
    systemPrompt += policy.systemPromptSection();

    // Qwen3 and its relatives reason inside a <think> block, and ThinkFilter
    // removes that block before anything is shown. Those tokens cost exactly
    // what a visible token costs and are then discarded, which on a short
    // question is most of the wait spent on text nobody reads. `/no_think` is
    // the hint the family understands.
    //
    // A hint, not a guarantee: a model that ignores it still produces a think
    // block, and ThinkFilter still strips it. Nothing downstream depends on
    // this working.
    if (!cloud && m_settings.suppressReasoning) {
        systemPrompt += "\n\n/no_think";
    }

    // The catalogue tells the model what exists and how to ask. It is
    // documentation, not authority: the same call is validated, checked and
    // possibly refused whatever the model was told here, and a model that
    // ignores every word of it cannot reach anything extra.
    if (!m_promptSection.isEmpty()) {
        systemPrompt += "\n\n";
        systemPrompt += m_promptSection.toStdString();
    }

    // The context manager owns the budget. Everything below is what actually
    // fits; the system prompt is Critical and survives compaction, so the
    // language rules and the tool catalogue are never the thing that gets
    // dropped to make room for chat.
    m_context.setSystemPrompt(std::move(systemPrompt));
    const bool writingArtifact = QRegularExpression(QStringLiteral("(?:создай|сделай|напиши|подготовь|create|write)"), QRegularExpression::CaseInsensitiveOption).match(m_latestUserRequest).hasMatch();
    const bool drawing = QRegularExpression(QStringLiteral("(?:нарисуй|рисуй|начерти|изобрази|дорисуй|объясни|обьясни|обясни)"), QRegularExpression::CaseInsensitiveOption).match(m_latestUserRequest).hasMatch();
    const int outputTokens = cloud
        ? std::clamp(std::max(m_settings.maxTokens, request.reasoning || drawing ? 8192 : writingArtifact ? 4096 : 1536), 1, 16384)
        : m_settings.maxTokens;
    if (cloud) {
        agent::ContextManager::Budget budget;
        budget.contextTokens = 32768;
        budget.outputReserveTokens = static_cast<std::size_t>(outputTokens) + 256;
        m_context.setBudget(budget);
    }

    const agent::PreparedContext prepared = m_context.prepare();
    if (prepared.overBudget) {
        // Even the rules do not fit. Sending anyway would get a backend error
        // a moment later; saying so now is the same outcome, explained.
        setError(tr("The conversation no longer fits in the model's context. "
                    "Start a new conversation."));
        concludeTurn({}, false);
        return;
    }

    for (const agent::ContextEntry& entry : prepared.entries) {
        switch (entry.role) {
        case agent::ContextRole::System:
            request.messages.push_back({llm::ChatMessage::Role::System, entry.content});
            break;
        case agent::ContextRole::Assistant:
            request.messages.push_back(
                {llm::ChatMessage::Role::Assistant, entry.content});
            break;
        case agent::ContextRole::User:
        case agent::ContextRole::ToolResult:
            // A tool result enters as a user-role message, framed as data. It
            // is never a system message: that is the one role whose text
            // carries authority.
            request.messages.push_back({llm::ChatMessage::Role::User, entry.content});
            break;
        }
    }

    request.sampling.temperature = m_settings.temperature;
    request.sampling.topP = m_settings.topP;
    request.sampling.topK = m_settings.topK;
    request.sampling.maxTokens = outputTokens;
    m_lastTokenBudget = outputTokens;
    m_reasoning = request.reasoning;

    m_thinkFilter.reset();
    m_utf8Decoder.reset();
    m_conversation.beginAssistant();
    setGenerating(true);

    dumpRequest(request);

    m_pool.post([this, request = std::move(request)] {
        // Batch bursts of tokens to avoid relayout and autoscroll for every token.
        //
        // A piece may end part-way through a multi-byte character - the backend
        // contract says so explicitly - so the bytes are accumulated and only
        // whole characters are decoded. Decoding each piece on its own turned
        // Cyrillic into replacement characters whenever a token boundary fell
        // inside a two-byte sequence, which is most of them.
        QString pendingText;
        QElapsedTimer deliveryClock;
        deliveryClock.start();
        const auto flushText = [this, &pendingText, &deliveryClock] {
            if (pendingText.isEmpty()) return;
            QString text = std::exchange(pendingText, {});
            deliveryClock.restart();
            QMetaObject::invokeMethod(
                this,
                [this, text] {
                    const QString visible = m_thinkFilter.feed(text);
                    if (!visible.isEmpty()) {
                        m_conversation.appendToAssistant(visible);
                        Q_EMIT assistantChunk(visible);
                    }
                },
                Qt::QueuedConnection);
        };
        const auto onToken = [this, &pendingText, &deliveryClock, &flushText](std::string_view piece) {
            pendingText += m_utf8Decoder.feed(piece);
            if (deliveryClock.elapsed() >= 33 || pendingText.size() >= 2048) flushText();
            return true;
        };

        core::Result<llm::GenerationStats> stats = m_backend->generate(request, onToken);
        flushText(); // Queued before completion so the final text is never lost.

        QMetaObject::invokeMethod(
            this,
            [this, stats = std::move(stats)] {
                // Release whatever the think filter was still holding before
                // the turn is read back, so the answer is complete.
                if (const QString tail = m_thinkFilter.flush(); !tail.isEmpty()) {
                    m_conversation.appendToAssistant(tail);
                    Q_EMIT assistantChunk(tail);
                }
                m_conversation.endAssistant();

                if (!stats) {
                    setError(QString::fromStdString(stats.error().toUserString()));
                    concludeTurn({}, false);
                    return;
                }

                // Keep the assistant turn in history so the next message has
                // context. The conversation model holds the visible text.
                QString answer;
                const int lastRow = m_conversation.rowCount() - 1;
                if (lastRow >= 0) {
                    answer = m_conversation
                                 .data(m_conversation.index(lastRow, 0),
                                       ConversationModel::TextRole)
                                 .toString();
                    m_context.addAssistant(answer.toStdString());
                }

                m_lastGeneratedTokens = stats->generatedTokens;

                m_lastStats = tr("%1 tokens · %2 tok/s · prompt %3 tokens")
                                  .arg(stats->generatedTokens)
                                  .arg(stats->generatedTokensPerSecond(), 0, 'f', 1)
                                  .arg(stats->promptTokens);
                Q_EMIT lastStatsChanged();

                concludeTurn(answer, true);
            },
            Qt::QueuedConnection);
    });
}

void LlmController::dumpRequest(const llm::GenerationRequest& request) const {
    // Diagnostic only, and only when asked for by name. There is no user-facing
    // switch and nothing writes here by default; it exists because working out
    // what the model was actually shown by reading the code did not work.
    static const QByteArray target = qgetenv("JARVIS_PROMPT_DUMP");
    if (target.isEmpty()) {
        return;
    }

    QFile file{QString::fromUtf8(target)};
    if (!file.open(QIODevice::Append | QIODevice::WriteOnly)) {
        return;
    }

    std::size_t characters = 0;
    for (const llm::ChatMessage& message : request.messages) {
        characters += message.content.size();
    }

    file.write(QStringLiteral("=== request: %1 message(s), %2 characters ===\n")
                   .arg(request.messages.size())
                   .arg(characters)
                   .toUtf8());

    for (const llm::ChatMessage& message : request.messages) {
        const char* role = "USER";
        switch (message.role) {
        case llm::ChatMessage::Role::System:    role = "SYSTEM"; break;
        case llm::ChatMessage::Role::Assistant: role = "ASSISTANT"; break;
        case llm::ChatMessage::Role::User:      role = "USER"; break;
        }
        file.write(QStringLiteral("--- %1 (%2 chars) ---\n")
                       .arg(QString::fromUtf8(role))
                       .arg(message.content.size())
                       .toUtf8());
        file.write(message.content.data(),
                   static_cast<qint64>(message.content.size()));
        file.write("\n");
    }
    file.write("\n");
}

void LlmController::concludeTurn(const QString& answer, bool ok) {
    setGenerating(false);
    updateCoreState();
    Q_EMIT generationCompleted(answer, ok);
}

} // namespace jarvis::app
