#include "Bootstrap.h"

#include <QCoreApplication>

#include <cassert>
#include <exception>
#include <format>
#include <utility>

#include "jarvis/core/Version.h"
#include "jarvis/logging/ConsoleSink.h"
#include "jarvis/logging/FileSink.h"
#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "bootstrap";

} // namespace

Bootstrap::~Bootstrap() {
    shutdown();
}

core::Status Bootstrap::initialise() {
    using namespace jarvis::core;

    // 1. Where do we live?
    //
    // If the standard per-user location is unusable (redirected profile, full
    // disk, locked-down policy), fall back to a folder beside the executable
    // rather than refusing to start. The fallback is recorded as a warning so
    // the user finds out where their data actually went.
    const QString dataRoot = qEnvironmentVariable("JARVIS_DATA_ROOT");
    core::Result<config::AppPaths> paths = dataRoot.isEmpty() ? config::AppPaths::resolve()
        : config::AppPaths::resolveWithRoot(std::filesystem::path{dataRoot.toStdWString()});
    core::Status prepared = paths ? paths->ensureDirectories()
                                  : core::Status{std::unexpect, paths.error()};

    if (!prepared) {
        const std::string reason = prepared.error().toUserString();

        const std::filesystem::path fallbackRoot =
            std::filesystem::path{QCoreApplication::applicationDirPath().toStdWString()} /
            "JARVIS-data";

        core::Result<config::AppPaths> fallback =
            config::AppPaths::resolveWithRoot(fallbackRoot);
        if (!fallback) {
            return std::unexpected{fallback.error()};
        }
        if (core::Status created = fallback->ensureDirectories(); !created) {
            return created;
        }

        paths = std::move(fallback);
        m_report.warnings.push_back(
            std::format("Standard data location unavailable ({}). Using '{}' instead.",
                        reason, paths->root().string()));
    }

    m_paths = std::move(*paths);

    // 2. Read the configuration. A corrupt file degrades to defaults; the
    //    parse error is kept as a warning so the UI can show it.
    m_configStore = std::make_unique<config::ConfigStore>(m_paths->configFile());

    std::optional<core::Error> configError;
    config::ConfigStore::LoadResult loaded = m_configStore->loadOrDefault(&configError);
    m_config = loaded.config;
    m_report.configCreated = loaded.createdDefaults;

    if (configError) {
        m_report.warnings.push_back(
            std::format("Configuration could not be read, defaults are in use. {}",
                        configError->toUserString()));
    }
    for (const std::string& adjustment : loaded.validation.adjustments) {
        m_report.warnings.push_back(std::format("Configuration adjusted - {}", adjustment));
    }

    // 3. Start logging with the level the config asked for.
    logging::Logger& logger = logging::Logger::instance();
    logger.setLevel(m_config.logging.level);

    logging::FileSink::Options fileOptions;
    fileOptions.directory = m_paths->logDirectory();
    fileOptions.retentionDays = m_config.logging.retentionDays;
    fileOptions.maxFileBytes = m_config.logging.maxFileBytes;

    core::Result<std::unique_ptr<logging::FileSink>> fileSink =
        logging::FileSink::create(std::move(fileOptions));

    if (fileSink) {
        m_logFile = (*fileSink)->currentFile();
        m_report.logFileReady = true;
        logger.addSink(std::shared_ptr<logging::ILogSink>{std::move(*fileSink)});
    } else {
        m_report.warnings.push_back(
            std::format("File logging is unavailable. {}", fileSink.error().toUserString()));
    }

    if (m_config.logging.logToConsole) {
        logger.addSink(std::make_shared<logging::ConsoleSink>());
    }

    // 4. Worker pool. Its exception handler is wired to the logger so a task
    //    that throws leaves a trace instead of vanishing.
    m_threadPool = std::make_unique<core::ThreadPool>();
    m_threadPool->setExceptionHandler([](std::exception_ptr eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& ex) {
            JARVIS_LOG_ERROR(kCategory, "worker task threw: {}", ex.what());
        } catch (...) {
            JARVIS_LOG_ERROR(kCategory, "worker task threw a non-standard exception");
        }
    });

    m_initialised = true;

    JARVIS_LOG_INFO(kCategory, "{}", core::buildSummary());
    JARVIS_LOG_INFO(kCategory, "root      : {}", m_paths->root().string());
    JARVIS_LOG_INFO(kCategory, "config    : {}", m_paths->configFile().string());
    JARVIS_LOG_INFO(kCategory, "log file  : {}",
                    m_report.logFileReady ? m_logFile.string() : std::string{"<unavailable>"});
    JARVIS_LOG_INFO(kCategory, "log level : {}",
                    logging::toConfigName(m_config.logging.level));

    for (const std::string& warning : m_report.warnings) {
        JARVIS_LOG_WARN(kCategory, "{}", warning);
    }

    return ok();
}

const config::AppPaths& Bootstrap::paths() const {
    assert(m_paths.has_value() && "Bootstrap::initialise() must succeed first");
    return *m_paths;
}

config::ConfigStore& Bootstrap::configStore() {
    assert(m_configStore != nullptr && "Bootstrap::initialise() must succeed first");
    return *m_configStore;
}

core::ThreadPool& Bootstrap::threadPool() {
    assert(m_threadPool != nullptr && "Bootstrap::initialise() must succeed first");
    return *m_threadPool;
}

std::filesystem::path Bootstrap::activeLogFile() const {
    return m_logFile;
}

core::Status Bootstrap::updateConfig(const config::AppConfig& config) {
    if (!m_configStore) {
        return core::fail(core::ErrorCode::Unavailable, "configuration store is not initialised");
    }

    const core::Status saved = m_configStore->save(config);
    if (!saved) return saved;
    m_config = config;
    logging::Logger::instance().setLevel(m_config.logging.level);
    return core::ok();
}

void Bootstrap::shutdown() {
    if (!m_initialised) {
        return;
    }
    m_initialised = false;

    JARVIS_LOG_INFO(kCategory, "shutdown");

    // Workers must stop before the sinks they log to are torn down.
    if (m_threadPool) {
        m_threadPool->shutdown();
    }

    logging::Logger& logger = logging::Logger::instance();
    logger.flush();
    logger.clearSinks();
}

} // namespace jarvis::app
