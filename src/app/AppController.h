#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

#include "jarvis/config/AppConfig.h"

namespace jarvis::app {

class Bootstrap;
class TranslationManager;

/// The QML-facing façade over the C++ foundation.
///
/// Everything exposed here is backed by a real value or a real action. Nothing
/// on this object is a placeholder: if a capability does not exist yet, it is
/// absent from the interface rather than stubbed out.
/// Registered from main() with qmlRegisterSingletonInstance() as
/// `App` in the `Jarvis.App` import namespace.
class AppController : public QObject {
    Q_OBJECT

    // Build identity
    Q_PROPERTY(QString applicationName READ applicationName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString buildType READ buildType CONSTANT)
    Q_PROPERTY(QString compiler READ compiler CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString cxxStandard READ cxxStandard CONSTANT)
    Q_PROPERTY(QString buildStamp READ buildStamp CONSTANT)

    // On-disk locations
    Q_PROPERTY(QString rootPath READ rootPath CONSTANT)
    Q_PROPERTY(QString configFilePath READ configFilePath CONSTANT)
    Q_PROPERTY(QString logDirectoryPath READ logDirectoryPath CONSTANT)
    Q_PROPERTY(QString logFilePath READ logFilePath CONSTANT)
    Q_PROPERTY(bool logFileReady READ logFileReady CONSTANT)

    // Startup diagnostics
    Q_PROPERTY(QStringList startupWarnings READ startupWarnings CONSTANT)

    // Language
    Q_PROPERTY(QString language READ language NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList languageOptions READ languageOptions CONSTANT)

    // Live settings, each backed by config.json
    Q_PROPERTY(QString logLevel READ logLevel NOTIFY settingsChanged)
    Q_PROPERTY(QStringList logLevelOptions READ logLevelOptions CONSTANT)
    Q_PROPERTY(bool logToConsole READ logToConsole NOTIFY settingsChanged)
    Q_PROPERTY(bool rememberGeometry READ rememberGeometry NOTIFY settingsChanged)
    Q_PROPERTY(QString responseMode READ responseMode NOTIFY settingsChanged)
    Q_PROPERTY(bool jarvisPersonality READ jarvisPersonality NOTIFY settingsChanged)
    Q_PROPERTY(bool speechGpu READ speechGpu NOTIFY settingsChanged)
    Q_PROPERTY(int speechThreads READ speechThreads NOTIFY settingsChanged)
    Q_PROPERTY(int logRetentionDays READ logRetentionDays NOTIFY settingsChanged)

public:
    AppController(Bootstrap& bootstrap,
                  TranslationManager& translations,
                  QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] QString applicationName() const;
    [[nodiscard]] QString version() const;
    [[nodiscard]] QString buildType() const;
    [[nodiscard]] QString compiler() const;
    [[nodiscard]] QString qtVersion() const;
    [[nodiscard]] QString cxxStandard() const;
    [[nodiscard]] QString buildStamp() const;

    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] QString configFilePath() const;
    [[nodiscard]] QString logDirectoryPath() const;
    [[nodiscard]] QString logFilePath() const;
    [[nodiscard]] bool logFileReady() const;

    [[nodiscard]] QStringList startupWarnings() const;

    /// ISO code of the active language: "ru" or "en".
    [[nodiscard]] QString language() const;

    /// [{ code, name }] for the language picker; `name` is each language's own
    /// name for itself, never translated.
    [[nodiscard]] QVariantList languageOptions() const;

    /// Switches the interface language and persists the choice. Applies at once
    /// - no restart. Returns false if the catalogue is missing or the save
    /// failed, in which case the previous language stays active.
    Q_INVOKABLE bool setLanguage(const QString& code);

    [[nodiscard]] QString logLevel() const;
    [[nodiscard]] QStringList logLevelOptions() const;
    [[nodiscard]] bool logToConsole() const;
    [[nodiscard]] bool rememberGeometry() const;
    [[nodiscard]] int logRetentionDays() const;

    /// Applies a log level and writes it to config.json. Takes effect at once.
    /// Returns false when the name is not a valid level or the save failed.
    Q_INVOKABLE bool setLogLevel(const QString& level);

    /// Console logging only has an effect on the next start, because sinks are
    /// attached during bootstrap; the setting is persisted immediately.
    Q_INVOKABLE bool setLogToConsole(bool enabled);

    Q_INVOKABLE bool setRememberGeometry(bool enabled);
    QString responseMode() const;
    bool jarvisPersonality() const;
    bool speechGpu() const;
    int speechThreads() const;
    Q_INVOKABLE bool setResponseMode(const QString& mode);
    Q_INVOKABLE bool setJarvisPersonality(bool enabled);
    Q_INVOKABLE bool setSpeechGpu(bool enabled);
    Q_INVOKABLE bool setSpeechThreads(int threads);
    Q_INVOKABLE QVariantMap inspectTextFile(const QUrl& url);

    /// Opens the log folder in Explorer. Returns false if the shell refused.
    Q_INVOKABLE bool openLogDirectory();

    /// Opens config.json in the user's default editor.
    Q_INVOKABLE bool openConfigFile();

    /// Copies the build and path summary to the clipboard, for bug reports.
    Q_INVOKABLE void copyDiagnostics();

    /// Persists window size and position when geometry memory is enabled.
    /// Returns false and logs when the config could not be written.
    Q_INVOKABLE bool saveWindowGeometry(int width, int height, int x, int y);

    /// Stored geometry for the initial window, already validated.
    Q_INVOKABLE int storedWindowWidth() const;
    Q_INVOKABLE int storedWindowHeight() const;
    Q_INVOKABLE int storedWindowX() const;
    Q_INVOKABLE int storedWindowY() const;
    Q_INVOKABLE bool remembersGeometry() const;

    Q_INVOKABLE void logInfo(const QString& category, const QString& message);

Q_SIGNALS:
    void settingsChanged();
    void diagnosticsCopied();
    void settingsSaveFailed(const QString& reason);

private:
    [[nodiscard]] QString diagnosticsText() const;
    [[nodiscard]] bool persist(const config::AppConfig& updated, const char* what);

    Bootstrap& m_bootstrap;
    TranslationManager& m_translations;
};

} // namespace jarvis::app
