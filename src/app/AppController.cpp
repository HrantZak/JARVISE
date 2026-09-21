#include "AppController.h"

#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QUrl>

#include <QVariantMap>

#include <optional>

#include "Bootstrap.h"
#include "TranslationManager.h"
#include "jarvis/core/Version.h"
#include "jarvis/i18n/Language.h"
#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "ui";

QString toQString(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

AppController::AppController(Bootstrap& bootstrap,
                             TranslationManager& translations,
                             QObject* parent)
    : QObject{parent}
    , m_bootstrap{bootstrap}
    , m_translations{translations} {}

AppController::~AppController() = default;

QString AppController::applicationName() const {
    return QStringLiteral("JARVIS");
}

QString AppController::version() const {
    return toQString(core::buildInfo().version);
}

QString AppController::buildType() const {
    return toQString(core::buildInfo().buildType);
}

QString AppController::compiler() const {
    return toQString(core::buildInfo().compiler);
}

QString AppController::qtVersion() const {
    return toQString(core::buildInfo().qtVersion);
}

QString AppController::cxxStandard() const {
    return toQString(core::buildInfo().cxxStandard);
}

QString AppController::buildStamp() const {
    const core::BuildInfo& info = core::buildInfo();
    return toQString(info.buildDate) + QStringLiteral(" ") + toQString(info.buildTime);
}

QString AppController::rootPath() const {
    return toQString(m_bootstrap.paths().root());
}

QString AppController::configFilePath() const {
    return toQString(m_bootstrap.paths().configFile());
}

QString AppController::logDirectoryPath() const {
    return toQString(m_bootstrap.paths().logDirectory());
}

QString AppController::logFilePath() const {
    return toQString(m_bootstrap.activeLogFile());
}

bool AppController::logFileReady() const {
    return m_bootstrap.report().logFileReady;
}

QStringList AppController::startupWarnings() const {
    QStringList list;
    for (const std::string& warning : m_bootstrap.report().warnings) {
        list.append(QString::fromStdString(warning));
    }
    return list;
}

QString AppController::language() const {
    return toQString(i18n::languageCode(m_bootstrap.config().general.language));
}

QVariantList AppController::languageOptions() const {
    QVariantList options;
    for (const i18n::Language language : i18n::kSupportedLanguages) {
        QVariantMap entry;
        entry[QStringLiteral("code")] = toQString(i18n::languageCode(language));
        // Endonyms stay in their own language: a Russian speaker looking for
        // their language should see "Русский", not "Russian".
        entry[QStringLiteral("name")] = toQString(i18n::languageEndonym(language));
        options.append(entry);
    }
    return options;
}

bool AppController::setLanguage(const QString& code) {
    const std::optional<i18n::Language> parsed =
        i18n::languageFromCode(code.toStdString());
    if (!parsed) {
        JARVIS_LOG_WARN(kCategory, "ignoring unsupported language '{}'", code.toStdString());
        return false;
    }

    if (m_bootstrap.config().general.language == *parsed) {
        return true;
    }

    // Switch first: if the catalogue is missing, nothing is persisted and the
    // interface keeps the language it was already speaking.
    if (!m_translations.apply(*parsed)) {
        Q_EMIT settingsSaveFailed(tr("Translations for this language are unavailable."));
        return false;
    }

    config::AppConfig updated = m_bootstrap.config();
    updated.general.language = *parsed;
    return persist(updated, "language");
}

QString AppController::logLevel() const {
    return toQString(logging::toConfigName(m_bootstrap.config().logging.level));
}

QStringList AppController::logLevelOptions() const {
    return {QStringLiteral("trace"),   QStringLiteral("debug"), QStringLiteral("info"),
            QStringLiteral("warning"), QStringLiteral("error"), QStringLiteral("critical"),
            QStringLiteral("off")};
}

bool AppController::logToConsole() const {
    return m_bootstrap.config().logging.logToConsole;
}

bool AppController::rememberGeometry() const {
    return m_bootstrap.config().window.rememberGeometry;
}

int AppController::logRetentionDays() const {
    return m_bootstrap.config().logging.retentionDays;
}

bool AppController::persist(const config::AppConfig& updated, const char* what) {
    const core::Status saved = m_bootstrap.updateConfig(updated);
    if (!saved) {
        const QString reason = QString::fromStdString(saved.error().toUserString());
        JARVIS_LOG_WARN(kCategory, "cannot save {}: {}", what,
                        saved.error().toUserString());
        Q_EMIT settingsSaveFailed(reason);
        return false;
    }
    Q_EMIT settingsChanged();
    return true;
}

bool AppController::setLogLevel(const QString& level) {
    const std::optional<logging::LogLevel> parsed =
        logging::logLevelFromString(level.toStdString());
    if (!parsed) {
        JARVIS_LOG_WARN(kCategory, "ignoring unknown log level '{}'", level.toStdString());
        return false;
    }

    config::AppConfig updated = m_bootstrap.config();
    if (updated.logging.level == *parsed) {
        return true;
    }
    updated.logging.level = *parsed;

    // updateConfig() applies the level to the live logger as well as saving it.
    if (!persist(updated, "log level")) {
        return false;
    }

    JARVIS_LOG_INFO(kCategory, "log level set to {}", level.toStdString());
    return true;
}

bool AppController::setLogToConsole(bool enabled) {
    config::AppConfig updated = m_bootstrap.config();
    if (updated.logging.logToConsole == enabled) {
        return true;
    }
    updated.logging.logToConsole = enabled;
    return persist(updated, "console logging setting");
}

bool AppController::setRememberGeometry(bool enabled) {
    config::AppConfig updated = m_bootstrap.config();
    if (updated.window.rememberGeometry == enabled) {
        return true;
    }
    updated.window.rememberGeometry = enabled;
    return persist(updated, "window geometry setting");
}

QString AppController::responseMode() const { return QString::fromStdString(m_bootstrap.config().llm.responseMode); }
bool AppController::jarvisPersonality() const { return m_bootstrap.config().llm.jarvisPersonality; }
bool AppController::speechGpu() const { return m_bootstrap.config().voice.sttUseGpu; }
int AppController::speechThreads() const { return m_bootstrap.config().voice.sttThreads; }
bool AppController::setResponseMode(const QString& mode) {
    if (mode != "balanced" && mode != "fast" && mode != "thorough") return false;
    auto config = m_bootstrap.config();
    config.llm.responseMode = mode.toStdString();
    return persist(config, "response mode");
}
bool AppController::setJarvisPersonality(bool enabled) {
    auto config = m_bootstrap.config();
    config.llm.jarvisPersonality = enabled;
    return persist(config, "assistant personality");
}
bool AppController::setSpeechGpu(bool enabled) {
    auto config = m_bootstrap.config();
    config.voice.sttUseGpu = enabled;
    return persist(config, "speech acceleration");
}
bool AppController::setSpeechThreads(int threads) {
    if (threads < 1 || threads > 8) return false;
    auto config = m_bootstrap.config();
    config.voice.sttThreads = threads;
    return persist(config, "speech CPU workers");
}

QVariantMap AppController::inspectTextFile(const QUrl& url) {
    if (!url.isLocalFile()) return {{"error", QStringLiteral("Выберите локальный текстовый файл.")}};
    QFile file{url.toLocalFile()};
    if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024)
        return {{"error", QStringLiteral("Файл недоступен или больше 128 КБ.")}};
    const QByteArray data = file.readAll();
    QStringDecoder decoder{QStringDecoder::Utf8};
    const QString text = decoder(data);
    if (data.contains('\0') || decoder.hasError())
        return {{"error", QStringLiteral("Поддерживаются текстовые файлы UTF-8.")}};
    return {{"name", QFileInfo{file}.fileName()}, {"text", text}, {"error", QString{}}};
}

bool AppController::openLogDirectory() {
    const std::filesystem::path& directory = m_bootstrap.paths().logDirectory();
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(directory)));
    if (!opened) {
        JARVIS_LOG_WARN(kCategory, "shell refused to open log directory '{}'",
                        directory.string());
    }
    return opened;
}

bool AppController::openConfigFile() {
    const std::filesystem::path& file = m_bootstrap.paths().configFile();
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(file)));
    if (!opened) {
        JARVIS_LOG_WARN(kCategory, "shell refused to open config file '{}'", file.string());
    }
    return opened;
}

QString AppController::diagnosticsText() const {
    QStringList lines;
    lines << applicationName() + QStringLiteral(" ") + version()
                 + QStringLiteral(" (") + buildType() + QStringLiteral(")")
          << QStringLiteral("Built    : ") + buildStamp()
          << QStringLiteral("Compiler : ") + compiler()
          << QStringLiteral("Standard : ") + cxxStandard()
          << QStringLiteral("Qt       : ") + qtVersion()
          << QStringLiteral("Root     : ") + rootPath()
          << QStringLiteral("Config   : ") + configFilePath()
          << QStringLiteral("Logs     : ") + logDirectoryPath()
          << QStringLiteral("Log level: ") + logLevel();

    const QStringList warnings = startupWarnings();
    if (!warnings.isEmpty()) {
        lines << QStringLiteral("Warnings :");
        for (const QString& warning : warnings) {
            lines << QStringLiteral("  - ") + warning;
        }
    }

    return lines.join(QChar::fromLatin1('\n'));
}

void AppController::copyDiagnostics() {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(diagnosticsText());
        JARVIS_LOG_INFO(kCategory, "diagnostics copied to clipboard");
        Q_EMIT diagnosticsCopied();
    } else {
        JARVIS_LOG_WARN(kCategory, "clipboard is unavailable");
    }
}

bool AppController::saveWindowGeometry(int width, int height, int x, int y) {
    config::AppConfig updated = m_bootstrap.config();
    if (!updated.window.rememberGeometry) {
        return true;
    }

    updated.window.width = width;
    updated.window.height = height;
    updated.window.x = x;
    updated.window.y = y;

    // Clamp before persisting so a rogue value never reaches disk.
    const config::ValidationReport report = config::validate(updated);
    for (const std::string& adjustment : report.adjustments) {
        JARVIS_LOG_DEBUG(kCategory, "geometry adjusted - {}", adjustment);
    }

    const core::Status saved = m_bootstrap.updateConfig(updated);
    if (!saved) {
        JARVIS_LOG_WARN(kCategory, "cannot save window geometry: {}",
                        saved.error().toUserString());
        return false;
    }
    return true;
}

int AppController::storedWindowWidth() const {
    return m_bootstrap.config().window.width;
}

int AppController::storedWindowHeight() const {
    return m_bootstrap.config().window.height;
}

int AppController::storedWindowX() const {
    return m_bootstrap.config().window.x;
}

int AppController::storedWindowY() const {
    return m_bootstrap.config().window.y;
}

bool AppController::remembersGeometry() const {
    return m_bootstrap.config().window.rememberGeometry;
}

void AppController::logInfo(const QString& category, const QString& message) {
    const std::string categoryText = category.toStdString();
    JARVIS_LOG_INFO(categoryText, "{}", message.toStdString());
}

} // namespace jarvis::app
