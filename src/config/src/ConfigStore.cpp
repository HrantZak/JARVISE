#include "jarvis/config/ConfigStore.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>

#include <format>
#include <utility>

namespace fs = std::filesystem;

namespace jarvis::config {
namespace {

QString toQString(const fs::path& path) {
    return QString::fromStdWString(path.wstring());
}

QJsonObject toJson(const AppConfig& config) {
    QJsonObject general;
    general["language"] = QString::fromUtf8(
        std::string{jarvis::i18n::languageCode(config.general.language)});

    QJsonObject logging;
    logging["level"] = QString::fromUtf8(
        std::string{jarvis::logging::toConfigName(config.logging.level)});
    logging["logToConsole"] = config.logging.logToConsole;
    logging["retentionDays"] = config.logging.retentionDays;
    logging["maxFileBytes"] = static_cast<qint64>(config.logging.maxFileBytes);

    QJsonObject window;
    window["width"] = config.window.width;
    window["height"] = config.window.height;
    window["x"] = config.window.x;
    window["y"] = config.window.y;
    window["rememberGeometry"] = config.window.rememberGeometry;

    QJsonObject llm;
    llm["responseMode"] = QString::fromStdString(config.llm.responseMode);
    llm["jarvisPersonality"] = config.llm.jarvisPersonality;
    llm["modelPath"] = QString::fromStdString(config.llm.modelPath);
    llm["autoLoad"] = config.llm.autoLoad;
    llm["gpuLayers"] = config.llm.gpuLayers;
    llm["contextLength"] = static_cast<qint64>(config.llm.contextLength);
    llm["threads"] = static_cast<qint64>(config.llm.threads);
    llm["batchSize"] = static_cast<qint64>(config.llm.batchSize);
    llm["temperature"] = static_cast<double>(config.llm.temperature);
    llm["topP"] = static_cast<double>(config.llm.topP);
    llm["topK"] = config.llm.topK;
    llm["maxTokens"] = config.llm.maxTokens;
    llm["useOllamaModels"] = config.llm.useOllamaModels;
    llm["suppressReasoning"] = config.llm.suppressReasoning;

    QJsonArray modelDirectories;
    for (const std::string& directory : config.llm.modelDirectories) {
        modelDirectories.append(QString::fromStdString(directory));
    }
    llm["modelDirectories"] = modelDirectories;

    QJsonObject voice;
    voice["sttThreads"] = config.voice.sttThreads;
    voice["enabled"] = config.voice.enabled;
    voice["autoListen"] = config.voice.autoListen;
    voice["inputDeviceId"] = QString::fromStdString(config.voice.inputDeviceId);
    voice["outputDeviceId"] = QString::fromStdString(config.voice.outputDeviceId);
    voice["sttModelPath"] = QString::fromStdString(config.voice.sttModelPath);
    voice["sttUseGpu"] = config.voice.sttUseGpu;
    voice["ttsExecutable"] = QString::fromStdString(config.voice.ttsExecutable);
    voice["ttsRuVoice"] = QString::fromStdString(config.voice.ttsRuVoice);
    voice["ttsEnVoice"] = QString::fromStdString(config.voice.ttsEnVoice);
    voice["ttsLengthScale"] = static_cast<double>(config.voice.ttsLengthScale);
    voice["ttsVolume"] = static_cast<double>(config.voice.ttsVolume);
    voice["vadActivationThreshold"] =
        static_cast<double>(config.voice.vadActivationThreshold);
    voice["vadReleaseThreshold"] = static_cast<double>(config.voice.vadReleaseThreshold);
    voice["vadSilenceTimeoutMs"] = config.voice.vadSilenceTimeoutMs;
    voice["vadMinimumSpeechMs"] = config.voice.vadMinimumSpeechMs;
    voice["vadMaximumUtteranceMs"] = config.voice.vadMaximumUtteranceMs;
    voice["vadPreRollMs"] = config.voice.vadPreRollMs;

    QJsonObject tools;
    tools["enabled"] = config.tools.enabled;
    tools["allowReadOnly"] = config.tools.allowReadOnly;
    tools["allowSafeActions"] = config.tools.allowSafeActions;
    tools["allowConfirmedActions"] = config.tools.allowConfirmedActions;
    tools["maxRounds"] = config.tools.maxRounds;
    tools["confirmationTimeoutMs"] = config.tools.confirmationTimeoutMs;
    tools["auditEnabled"] = config.tools.auditEnabled;
    tools["auditCapacity"] = config.tools.auditCapacity;

    QJsonObject agent;
    agent["enabled"] = config.agent.enabled;
    agent["plannerEnabled"] = config.agent.plannerEnabled;
    agent["maxSteps"] = config.agent.maxSteps;
    agent["maxRetries"] = config.agent.maxRetries;
    agent["maxToolCalls"] = config.agent.maxToolCalls;
    agent["maxRuntimeMs"] = config.agent.maxRuntimeMs;
    agent["recoveryEnabled"] = config.agent.recoveryEnabled;
    agent["maxToolResultChars"] = config.agent.maxToolResultChars;
    agent["memoryEnabled"] = config.agent.memoryEnabled;
    agent["memoryPersistent"] = config.agent.memoryPersistent;
    agent["maxMemoryEntries"] = config.agent.maxMemoryEntries;
    agent["maxMemoryEntryChars"] = config.agent.maxMemoryEntryChars;

    QJsonObject root;
    root["configVersion"] = config.configVersion;
    root["agent"] = agent;
    root["general"] = general;
    root["llm"] = llm;
    root["logging"] = logging;
    root["tools"] = tools;
    root["voice"] = voice;
    root["window"] = window;
    return root;
}

int readInt(const QJsonObject& object, const char* key, int fallback) {
    const QJsonValue value = object.value(QLatin1String{key});
    return value.isDouble() ? value.toInt(fallback) : fallback;
}

bool readBool(const QJsonObject& object, const char* key, bool fallback) {
    const QJsonValue value = object.value(QLatin1String{key});
    return value.isBool() ? value.toBool() : fallback;
}

/// Reads \p key, appending a note to \p report when the stored value is present
/// but unusable, so a typo in the config file is visible instead of silent.
AppConfig fromJson(const QJsonObject& root, ValidationReport& report) {
    AppConfig config;

    config.configVersion = readInt(root, "configVersion", kCurrentConfigVersion);

    const QJsonObject general = root.value(QLatin1String{"general"}).toObject();
    if (!general.isEmpty()) {
        const QJsonValue languageValue = general.value(QLatin1String{"language"});
        if (languageValue.isString()) {
            // toStdString() gives UTF-8; the code is ASCII but the round trip
            // has to be UTF-8-clean for every other string in this file.
            const std::string code = languageValue.toString().toStdString();
            if (const auto parsed = jarvis::i18n::languageFromCode(code)) {
                config.general.language = *parsed;
            } else {
                report.adjustments.push_back(
                    std::format("general.language: '{}' is not a supported language, using '{}'",
                                code,
                                jarvis::i18n::languageCode(config.general.language)));
            }
        }
    }

    const QJsonObject logging = root.value(QLatin1String{"logging"}).toObject();
    if (!logging.isEmpty()) {
        const QJsonValue levelValue = logging.value(QLatin1String{"level"});
        if (levelValue.isString()) {
            const std::string levelName = levelValue.toString().toStdString();
            if (const auto parsed = jarvis::logging::logLevelFromString(levelName)) {
                config.logging.level = *parsed;
            } else {
                report.adjustments.push_back(
                    std::format("logging.level: '{}' is not a known level, using '{}'",
                                levelName,
                                jarvis::logging::toConfigName(config.logging.level)));
            }
        }

        config.logging.logToConsole =
            readBool(logging, "logToConsole", config.logging.logToConsole);
        config.logging.retentionDays =
            readInt(logging, "retentionDays", config.logging.retentionDays);

        const QJsonValue maxBytes = logging.value(QLatin1String{"maxFileBytes"});
        if (maxBytes.isDouble()) {
            const qint64 raw = maxBytes.toInteger(
                static_cast<qint64>(config.logging.maxFileBytes));
            if (raw > 0) {
                config.logging.maxFileBytes = static_cast<std::size_t>(raw);
            } else {
                report.adjustments.push_back(
                    std::format("logging.maxFileBytes: {} is not positive, using {}",
                                raw, config.logging.maxFileBytes));
            }
        }
    }

    const QJsonObject llm = root.value(QLatin1String{"llm"}).toObject();
    if (!llm.isEmpty()) {
        config.llm.responseMode = llm.value("responseMode").toString(QStringLiteral("balanced")).toStdString();
        config.llm.jarvisPersonality = readBool(llm, "jarvisPersonality", true);
        const QJsonValue path = llm.value(QLatin1String{"modelPath"});
        if (path.isString()) {
            config.llm.modelPath = path.toString().toStdString();
        }
        config.llm.autoLoad = readBool(llm, "autoLoad", config.llm.autoLoad);
        config.llm.gpuLayers = readInt(llm, "gpuLayers", config.llm.gpuLayers);
        config.llm.contextLength = static_cast<std::uint32_t>(
            readInt(llm, "contextLength", static_cast<int>(config.llm.contextLength)));
        config.llm.threads = static_cast<std::uint32_t>(
            readInt(llm, "threads", static_cast<int>(config.llm.threads)));
        config.llm.batchSize = static_cast<std::uint32_t>(
            readInt(llm, "batchSize", static_cast<int>(config.llm.batchSize)));
        config.llm.topK = readInt(llm, "topK", config.llm.topK);
        config.llm.maxTokens = readInt(llm, "maxTokens", config.llm.maxTokens);
        config.llm.useOllamaModels =
            readBool(llm, "useOllamaModels", config.llm.useOllamaModels);
        config.llm.suppressReasoning =
            readBool(llm, "suppressReasoning", config.llm.suppressReasoning);

        if (const QJsonValue directories = llm.value(QLatin1String{"modelDirectories"});
            directories.isArray()) {
            config.llm.modelDirectories.clear();
            for (const QJsonValue& entry : directories.toArray()) {
                if (entry.isString() && !entry.toString().isEmpty()) {
                    config.llm.modelDirectories.push_back(entry.toString().toStdString());
                }
            }
        }

        const QJsonValue temperature = llm.value(QLatin1String{"temperature"});
        if (temperature.isDouble()) {
            config.llm.temperature = static_cast<float>(temperature.toDouble());
        }
        const QJsonValue topP = llm.value(QLatin1String{"topP"});
        if (topP.isDouble()) {
            config.llm.topP = static_cast<float>(topP.toDouble());
        }
    }

    const QJsonObject voice = root.value(QLatin1String{"voice"}).toObject();
    if (!voice.isEmpty()) {
        config.voice.sttThreads = readInt(voice, "sttThreads", config.voice.sttThreads);
        const auto readString = [&voice](const char* key, std::string& target) {
            const QJsonValue value = voice.value(QLatin1String{key});
            if (value.isString()) {
                target = value.toString().toStdString();
            }
        };
        const auto readFloat = [&voice](const char* key, float& target) {
            const QJsonValue value = voice.value(QLatin1String{key});
            if (value.isDouble()) {
                target = static_cast<float>(value.toDouble());
            }
        };

        config.voice.enabled = readBool(voice, "enabled", config.voice.enabled);
        config.voice.autoListen = readBool(voice, "autoListen", config.voice.autoListen);
        readString("inputDeviceId", config.voice.inputDeviceId);
        readString("outputDeviceId", config.voice.outputDeviceId);
        readString("sttModelPath", config.voice.sttModelPath);
        config.voice.sttUseGpu = readBool(voice, "sttUseGpu", config.voice.sttUseGpu);
        readString("ttsExecutable", config.voice.ttsExecutable);
        readString("ttsRuVoice", config.voice.ttsRuVoice);
        readString("ttsEnVoice", config.voice.ttsEnVoice);
        readFloat("ttsLengthScale", config.voice.ttsLengthScale);
        readFloat("ttsVolume", config.voice.ttsVolume);
        readFloat("vadActivationThreshold", config.voice.vadActivationThreshold);
        readFloat("vadReleaseThreshold", config.voice.vadReleaseThreshold);
        config.voice.vadSilenceTimeoutMs =
            readInt(voice, "vadSilenceTimeoutMs", config.voice.vadSilenceTimeoutMs);
        config.voice.vadMinimumSpeechMs =
            readInt(voice, "vadMinimumSpeechMs", config.voice.vadMinimumSpeechMs);
        config.voice.vadMaximumUtteranceMs =
            readInt(voice, "vadMaximumUtteranceMs", config.voice.vadMaximumUtteranceMs);
        config.voice.vadPreRollMs =
            readInt(voice, "vadPreRollMs", config.voice.vadPreRollMs);
    }

    // Absent in a v4 file. Leaving the struct at its defaults is the migration:
    // the defaults are the safe setting, so an older config gains read-only
    // tools and keeps asking before anything with an effect.
    const QJsonObject tools = root.value(QLatin1String{"tools"}).toObject();
    if (!tools.isEmpty()) {
        config.tools.enabled = readBool(tools, "enabled", config.tools.enabled);
        config.tools.allowReadOnly =
            readBool(tools, "allowReadOnly", config.tools.allowReadOnly);
        config.tools.allowSafeActions =
            readBool(tools, "allowSafeActions", config.tools.allowSafeActions);
        config.tools.allowConfirmedActions =
            readBool(tools, "allowConfirmedActions", config.tools.allowConfirmedActions);
        config.tools.maxRounds = readInt(tools, "maxRounds", config.tools.maxRounds);
        config.tools.confirmationTimeoutMs =
            readInt(tools, "confirmationTimeoutMs", config.tools.confirmationTimeoutMs);
        config.tools.auditEnabled =
            readBool(tools, "auditEnabled", config.tools.auditEnabled);
        config.tools.auditCapacity =
            readInt(tools, "auditCapacity", config.tools.auditCapacity);
    }

    // Absent in a v5 file. As with the tools section, leaving the struct at its
    // defaults is the migration: the defaults are the safe setting.
    const QJsonObject agent = root.value(QLatin1String{"agent"}).toObject();
    if (!agent.isEmpty()) {
        config.agent.enabled = readBool(agent, "enabled", config.agent.enabled);
        config.agent.plannerEnabled =
            readBool(agent, "plannerEnabled", config.agent.plannerEnabled);
        config.agent.maxSteps = readInt(agent, "maxSteps", config.agent.maxSteps);
        config.agent.maxRetries = readInt(agent, "maxRetries", config.agent.maxRetries);
        config.agent.maxToolCalls =
            readInt(agent, "maxToolCalls", config.agent.maxToolCalls);
        config.agent.maxRuntimeMs =
            readInt(agent, "maxRuntimeMs", config.agent.maxRuntimeMs);
        config.agent.recoveryEnabled =
            readBool(agent, "recoveryEnabled", config.agent.recoveryEnabled);
        config.agent.maxToolResultChars =
            readInt(agent, "maxToolResultChars", config.agent.maxToolResultChars);
        config.agent.memoryEnabled =
            readBool(agent, "memoryEnabled", config.agent.memoryEnabled);
        config.agent.memoryPersistent =
            readBool(agent, "memoryPersistent", config.agent.memoryPersistent);
        config.agent.maxMemoryEntries =
            readInt(agent, "maxMemoryEntries", config.agent.maxMemoryEntries);
        config.agent.maxMemoryEntryChars =
            readInt(agent, "maxMemoryEntryChars", config.agent.maxMemoryEntryChars);
    }

    const QJsonObject window = root.value(QLatin1String{"window"}).toObject();
    if (!window.isEmpty()) {
        config.window.width = readInt(window, "width", config.window.width);
        config.window.height = readInt(window, "height", config.window.height);
        config.window.x = readInt(window, "x", config.window.x);
        config.window.y = readInt(window, "y", config.window.y);
        config.window.rememberGeometry =
            readBool(window, "rememberGeometry", config.window.rememberGeometry);
    }

    return config;
}

} // namespace

ConfigStore::ConfigStore(fs::path configFile)
    : m_file{std::move(configFile)} {}

core::Result<ConfigStore::LoadResult> ConfigStore::load() {
    using namespace jarvis::core;

    std::lock_guard lock{m_mutex};

    QFile file{toQString(m_file)};
    if (!file.exists()) {
        LoadResult result;
        result.validation = validate(result.config);
        result.createdDefaults = true;

        // Materialise the defaults so the user has a real file to edit.
        const QJsonDocument document{toJson(result.config)};
        QSaveFile saver{toQString(m_file)};
        if (saver.open(QIODevice::WriteOnly | QIODevice::Text)) {
            saver.write(document.toJson(QJsonDocument::Indented));
            if (!saver.commit()) {
                return fail(ErrorCode::IoFailure,
                            std::format("cannot write default config to '{}'",
                                        m_file.string()));
            }
        } else {
            return fail(ErrorCode::IoFailure,
                        std::format("cannot create config file '{}': {}",
                                    m_file.string(),
                                    saver.errorString().toStdString()));
        }
        return result;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail(ErrorCode::IoFailure,
                    std::format("cannot open config file '{}': {}",
                                m_file.string(), file.errorString().toStdString()));
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(ErrorCode::ParseFailure,
                    std::format("config file '{}' is not valid JSON at offset {}: {}",
                                m_file.string(),
                                parseError.offset,
                                parseError.errorString().toStdString()));
    }
    if (!document.isObject()) {
        return fail(ErrorCode::ParseFailure,
                    std::format("config file '{}' must contain a JSON object",
                                m_file.string()));
    }

    LoadResult result;
    result.config = fromJson(document.object(), result.validation);

    ValidationReport clamped = validate(result.config);
    for (std::string& note : clamped.adjustments) {
        result.validation.adjustments.push_back(std::move(note));
    }

    return result;
}

ConfigStore::LoadResult ConfigStore::loadOrDefault(std::optional<core::Error>* outError) {
    core::Result<LoadResult> result = load();
    if (result) {
        if (outError != nullptr) {
            outError->reset();
        }
        return std::move(*result);
    }

    if (outError != nullptr) {
        *outError = result.error();
    }

    // A broken config must never stop JARVIS from starting.
    LoadResult fallback;
    fallback.validation = validate(fallback.config);
    return fallback;
}

core::Status ConfigStore::save(const AppConfig& config) {
    using namespace jarvis::core;

    std::lock_guard lock{m_mutex};

    const QJsonDocument document{toJson(config)};

    // QSaveFile writes to a temporary and renames on commit, so an interrupted
    // save cannot truncate the existing config.
    QSaveFile saver{toQString(m_file)};
    if (!saver.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return fail(ErrorCode::IoFailure,
                    std::format("cannot open config file '{}' for writing: {}",
                                m_file.string(), saver.errorString().toStdString()));
    }

    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (saver.write(payload) != payload.size()) {
        saver.cancelWriting();
        return fail(ErrorCode::IoFailure,
                    std::format("short write while saving config '{}'", m_file.string()));
    }

    if (!saver.commit()) {
        return fail(ErrorCode::IoFailure,
                    std::format("cannot commit config file '{}': {}",
                                m_file.string(), saver.errorString().toStdString()));
    }

    return ok();
}

} // namespace jarvis::config
