#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSurfaceFormat>
#include <QTimer>
#include <QQuickWindow>
#include <QDir>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include "AgentLoop.h"
#include "AiCoreModel.h"
#include "AppController.h"
#include "AudioCapture.h"
#include "AudioPlayer.h"
#include "Bootstrap.h"
#include "LlmController.h"
#include "DeepSeekBackend.h"
#include "DeepSeekConnection.h"
#include "MemoryController.h"
#include "SceneController.h"
#include "SceneGeometry.h"
#include "SystemMonitor.h"
#include "ToolCoordinator.h"
#include "DrawTool.h"
#include "TranslationManager.h"
#include "VoiceController.h"
#include "jarvis/llm/LlamaCppBackend.h"
#include "jarvis/voice/PiperTtsBackend.h"
#include "jarvis/voice/WhisperSttBackend.h"
#include "jarvis/core/Version.h"
#include "jarvis/logging/Logger.h"
#include "jarvis/system/WindowsSystemMetricsProvider.h"

using namespace jarvis;

namespace {

constexpr std::string_view kCategory = "app";

/// Routes Qt's own warnings into the JARVIS log so QML errors end up in the
/// same file as everything else instead of a console nobody sees.
void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    logging::LogLevel level = logging::LogLevel::Info;
    switch (type) {
    case QtDebugMsg:    level = logging::LogLevel::Debug; break;
    case QtInfoMsg:     level = logging::LogLevel::Info; break;
    case QtWarningMsg:  level = logging::LogLevel::Warning; break;
    case QtCriticalMsg: level = logging::LogLevel::Error; break;
    case QtFatalMsg:    level = logging::LogLevel::Critical; break;
    }

    logging::Logger& logger = logging::Logger::instance();
    if (!logger.isEnabled(level)) {
        return;
    }

    const std::source_location location = std::source_location::current();
    logger.log(level,
               context.category != nullptr ? context.category : "qt",
               message.toStdString(),
               location);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName(QStringLiteral("JARVIS"));
    QCoreApplication::setOrganizationName(QStringLiteral("JARVIS"));
    QCoreApplication::setApplicationVersion(
        QString::fromUtf8(core::buildInfo().version.data(),
                          static_cast<qsizetype>(core::buildInfo().version.size())));

    QGuiApplication application{argc, argv};
    const bool smokeTest = application.arguments().contains(QStringLiteral("--smoke-test"));
    if (application.arguments().contains(QStringLiteral("--check-api"))) {
        QByteArray key = app::DeepSeekConnection::savedKey();
        if (key.isEmpty()) key = qgetenv("DEEPSEEK_API_KEY").trimmed();
        if (key.isEmpty()) { std::fprintf(stderr, "DeepSeek check: no API key configured.\n"); return 4; }
        app::DeepSeekBackend backend{key, QStringLiteral("deepseek-v4-flash")};
        llm::GenerationRequest request;
        request.messages.push_back({llm::ChatMessage::Role::User, "Reply with OK only."});
        request.sampling.maxTokens = 32;
        const auto result = backend.generate(request, [](std::string_view) { return true; });
        if (!result) { std::fprintf(stderr, "%s\n", result.error().toUserString().c_str()); return 5; }
        std::fprintf(stdout, "DeepSeek API check passed.\n");
        return 0;
    }

    // Basic is the only style with no platform theming of its own, which is
    // what a fully custom HUD needs.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    app::Bootstrap bootstrap;
    if (const core::Status started = bootstrap.initialise(); !started) {
        // Nothing is logging yet at this point, so report on stderr and stop.
        std::fprintf(stderr, "JARVIS cannot start: %s\n", started.error().toString().c_str());
        return 1;
    }

    qInstallMessageHandler(qtMessageHandler);

    // Language before anything user-visible is built, so the first frame is
    // already in the right language rather than flashing English first.
    app::TranslationManager translations;
    if (!translations.apply(bootstrap.config().general.language)) {
        JARVIS_LOG_ERROR(kCategory,
                         "could not load the configured language; interface strings "
                         "will fall back to their source text");
    }

    app::AppController controller{bootstrap, translations};
    app::AiCoreModel aiCore;

    // Telemetry. The provider is constructed here and handed to the monitor,
    // which owns it and only touches it from the worker pool.
    auto metricsProvider = std::make_unique<system::WindowsSystemMetricsProvider>();
    const QString gpuReason =
        QString::fromStdString(metricsProvider->gpuUnavailableReason());

    app::SystemMonitor systemMonitor{bootstrap.threadPool(), std::move(metricsProvider),
                                     gpuReason};

    // Tools. Registration happens inside the coordinator's constructor and
    // never again: the set of things JARVIS can do is fixed here, before a
    // model exists to ask for any of them. The coordinator reads the monitor's
    // provider rather than sampling the machine a second time.
    scene3d::SceneController sceneController;
    app::ToolCoordinator toolCoordinator{bootstrap.threadPool(),
                                         systemMonitor.provider(), aiCore, nullptr, std::make_unique<app::DrawTool>(sceneController)};
    toolCoordinator.applySettings(bootstrap.config().tools);

    // Local model engine. The backend is constructed unconditionally; whether a
    // model is loaded is a separate, explicit decision the user makes.
    QByteArray deepseekKey = app::DeepSeekConnection::savedKey();
    if (deepseekKey.isEmpty()) deepseekKey = qgetenv("DEEPSEEK_API_KEY").trimmed();
    if (smokeTest) deepseekKey.clear();
    const bool useDeepSeek = !deepseekKey.isEmpty();
    std::unique_ptr<llm::ILLMBackend> backend;
    if (useDeepSeek) {
        QString model = qEnvironmentVariable("JARVIS_DEEPSEEK_MODEL").trimmed();
        if (model.isEmpty()) model = QStringLiteral("deepseek-v4-flash");
        backend = std::make_unique<app::DeepSeekBackend>(deepseekKey, model);
    }
    app::LlmController llmController{bootstrap.threadPool(), std::move(backend), aiCore};
    app::DeepSeekConnection deepseekConnection{llmController};
    llmController.applySettings(bootstrap.config().llm);
    QObject::connect(&controller, &app::AppController::settingsChanged, &llmController,
        [&] { llmController.applySettings(bootstrap.config().llm); });
    llmController.setLanguage(bootstrap.config().general.language);

    // Choosing a model is a decision worth remembering. Picking one on the
    // MODELS page now writes it to the configuration and turns auto-load on, so
    // the next start comes up with the same model rather than empty.
    //
    // Only ever recorded after a load *succeeded*: a path that failed is not
    // one to retry automatically on every start.
    QObject::connect(&llmController, &app::LlmController::modelLoaded,
                     &llmController, [&bootstrap, &llmController] {
        const std::string path = llmController.loadedModelPath().toStdString();
        if (path.empty()) {
            return;
        }

        config::AppConfig updated = bootstrap.config();
        if (updated.llm.modelPath == path && updated.llm.autoLoad) {
            return;  // already remembered; do not rewrite the file every load
        }

        updated.llm.modelPath = path;
        updated.llm.autoLoad = true;

        if (const core::Status saved = bootstrap.updateConfig(updated); !saved) {
            JARVIS_LOG_WARN(kCategory,
                            "could not remember the chosen model: {}",
                            saved.error().toUserString());
            return;
        }
        JARVIS_LOG_INFO(kCategory, "model remembered for the next start");
    });

    // --- Voice ------------------------------------------------------------
    //
    // Declared in dependency order so destruction runs in reverse: the
    // controller tears down first and stops the engines before they die.

    const config::VoiceSettings& voiceSettings = bootstrap.config().voice;

    // A relative model path used to be resolved against the process working
    // directory, which is only the repository root when the application is
    // launched from a shell sitting there. Double-clicked from bin\, the
    // default "models/whisper/..." resolved to a path under bin\ that does not
    // exist, and speech recognition reported NO MODEL with a 1.5 GB file
    // sitting in the tree.
    //
    // So a relative path is now tried against the places a model is actually
    // kept, in order, and the first one that exists wins. An absolute path is
    // taken as given - if a person wrote it down, it is not ours to second
    // guess. Nothing is downloaded and nothing is written; this only ever reads.
    const auto resolveModelPath = [&bootstrap](const std::string& configured)
        -> std::filesystem::path {
        if (configured.empty()) {
            return {};
        }

        const std::filesystem::path path{configured};
        if (path.is_absolute()) {
            return path;
        }

        std::error_code ec;
        std::vector<std::filesystem::path> roots;

        // Where the application keeps its own data, and where a user who
        // followed docs/BUILD.md would have put the file.
        roots.push_back(bootstrap.paths().root());

        // The repository, found by walking up from the executable rather than
        // by trusting the working directory: bin -> msvc-release -> build -> .
        const std::filesystem::path exeDir =
            std::filesystem::path{
                QCoreApplication::applicationDirPath().toStdWString()};
        for (std::filesystem::path dir = exeDir;
             !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path()) {
            roots.push_back(dir);
        }

        // Last, the working directory - the old behaviour, kept so that
        // launching from the repository root still works exactly as before.
        roots.push_back(std::filesystem::current_path(ec));

        for (const std::filesystem::path& root : roots) {
            const std::filesystem::path candidate = root / path;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }

        // Nothing matched. Return the old resolution so the failure is reported
        // against a concrete path rather than an empty one.
        return std::filesystem::absolute(path, ec);
    };

    voice::WhisperSttBackend::Options sttOptions;
    sttOptions.modelPath = resolveModelPath(voiceSettings.sttModelPath);
    sttOptions.useGpu = voiceSettings.sttUseGpu;
    sttOptions.threads = voiceSettings.sttThreads;

    app::AudioCapture audioCapture;
    app::AudioPlayer audioPlayer;
    voice::WhisperSttBackend sttBackend{sttOptions};
    const std::filesystem::path voiceHome =
        std::filesystem::path{qEnvironmentVariable("USERPROFILE").toStdWString()} / ".helolo" / "voice";
    voice::PiperTtsBackend::Options ttsOptions;
    ttsOptions.executable = voiceSettings.ttsExecutable.empty()
        ? voiceHome / "piper" / "piper.exe" : resolveModelPath(voiceSettings.ttsExecutable);
    ttsOptions.voices[i18n::Language::Russian] = voiceSettings.ttsRuVoice.empty()
        ? voiceHome / "models" / "ru_RU-dmitri-medium.onnx" : resolveModelPath(voiceSettings.ttsRuVoice);
    ttsOptions.voices[i18n::Language::English] = voiceSettings.ttsEnVoice.empty()
        ? voiceHome / "models" / "en_US-ryan-medium.onnx" : resolveModelPath(voiceSettings.ttsEnVoice);
    // A restrained delivery; preserve the approved speaker's natural pitch.
    ttsOptions.lengthScale = 1.04F;
    ttsOptions.sentenceSilence = 0.22F;
    ttsOptions.jarvisTone = true;
    ttsOptions.expressive = true;
    voice::PiperTtsBackend ttsBackend{ttsOptions};
    JARVIS_LOG_INFO(kCategory, "Neural voices: ru={}, en={}",
                    ttsBackend.voiceName(i18n::Language::Russian),
                    ttsBackend.voiceName(i18n::Language::English));

    audioPlayer.setVolume(voiceSettings.ttsVolume);
    if (!voiceSettings.inputDeviceId.empty()) {
        audioCapture.setDevice(QString::fromStdString(voiceSettings.inputDeviceId));
    }
    if (!voiceSettings.outputDeviceId.empty()) {
        audioPlayer.setDevice(QString::fromStdString(voiceSettings.outputDeviceId));
    }

    app::VoiceController::Dependencies voiceDeps;
    voiceDeps.pool = &bootstrap.threadPool();
    voiceDeps.capture = &audioCapture;
    voiceDeps.player = &audioPlayer;
    voiceDeps.stt = &sttBackend;
    voiceDeps.tts = &ttsBackend;
    voiceDeps.llm = &llmController;
    voiceDeps.core = &aiCore;

    // The agent owns orchestration. It is constructed after the model engine
    // and before the voice pipeline, because both submit requests through it
    // rather than driving the model themselves.
    app::AgentLoop agentLoop{llmController, toolCoordinator, aiCore};
    agentLoop.applySettings(bootstrap.config().agent);
    app::MemoryController memoryController{agentLoop, bootstrap};
    const auto memoryCommand=agentLoop.localCommand;
    agentLoop.localCommand=[&sceneController,memoryCommand](const QString& request)->std::optional<QString> {
        if(auto result=sceneController.handle(request)) return result;
        return memoryCommand?memoryCommand(request):std::nullopt;
    };
    QObject::connect(&sceneController,&scene3d::SceneController::drawingPrompt,&agentLoop,[&agentLoop](const QString& text){agentLoop.submit(text,false);});

    // Spoken requests take the same road as typed ones. Set after construction
    // of the agent, before construction of the controller that reads it.
    voiceDeps.agent = &agentLoop;

    app::VoiceController voiceController{voiceDeps};
    voiceController.setWakeWordRequired(true);
    deepseekConnection.setBusyGuard([&] {
        return agentLoop.busy() || toolCoordinator.busy() || voiceController.loading() ||
            (voiceController.isEnabled() && !voiceController.isListening());
    });
    voiceController.setLanguage(bootstrap.config().general.language);

    {
        voice::VoiceActivityDetector::Config vad;
        vad.activationThreshold = voiceSettings.vadActivationThreshold;
        vad.releaseThreshold = voiceSettings.vadReleaseThreshold;
        vad.silenceTimeoutMs = voiceSettings.vadSilenceTimeoutMs;
        vad.minimumSpeechMs = voiceSettings.vadMinimumSpeechMs;
        vad.maximumUtteranceMs = voiceSettings.vadMaximumUtteranceMs;
        vad.preRollMs = voiceSettings.vadPreRollMs;
        voiceController.setVadConfig(vad);
    }

    // Speech loading is asynchronous and owned by VoiceController.
    QQmlApplicationEngine engine;
    translations.setEngine(&engine);

    // QML retranslates itself; C++ objects that cached tr() results rebuild
    // theirs here.
    QObject::connect(&translations, &app::TranslationManager::languageChanged,
                     &aiCore, &app::AiCoreModel::retranslate);
    QObject::connect(&translations, &app::TranslationManager::languageChanged,
                     &systemMonitor, &app::SystemMonitor::retranslate);
    QObject::connect(&translations, &app::TranslationManager::languageChanged,
                     &toolCoordinator, &app::ToolCoordinator::retranslate);
    QObject::connect(&translations, &app::TranslationManager::languageChanged,
                     &agentLoop, &app::AgentLoop::retranslate);

    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "App", &controller);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Core", &aiCore);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Sys", &systemMonitor);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Llm", &llmController);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "DeepSeek", &deepseekConnection);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Memory", &memoryController);
    qmlRegisterSingletonInstance("Jarvis.Scene3D", 1, 0, "Scene3D", &sceneController);
    qmlRegisterType<scene3d::SceneGeometry>("Jarvis.Scene3D",1,0,"SceneGeometry");
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Voice", &voiceController);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Mic", &audioCapture);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Speaker", &audioPlayer);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Agent", &agentLoop);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Tools", &toolCoordinator);
    qmlRegisterSingletonInstance("Jarvis.App", 1, 0, "Confirm",
                                 toolCoordinator.confirmation());

    // The voice pipeline follows the interface language, through the same
    // LanguagePolicy the model prompt uses.
    QObject::connect(&translations, &app::TranslationManager::languageChanged,
                     &voiceController, [&voiceController, &bootstrap] {
                         voiceController.setLanguage(
                             bootstrap.config().general.language);
                     });

    // The Core's live audio levels are real measurements from the devices.
    QObject::connect(&audioCapture, &app::AudioCapture::inputLevelChanged, &aiCore,
                     [&aiCore, &audioCapture] {
                         aiCore.setInputLevel(audioCapture.inputLevel());
                     });
    QObject::connect(&audioPlayer, &app::AudioPlayer::outputLevelChanged, &aiCore,
                     [&aiCore, &audioPlayer] {
                         aiCore.setOutputLevel(audioPlayer.outputLevel());
                     });
    qmlRegisterUncreatableType<app::ConversationModel>(
        "Jarvis.App", 1, 0, "ConversationModel",
        QStringLiteral("ConversationModel is owned by Llm.conversation"));

    QObject::connect(&translations, &app::TranslationManager::languageChanged,
                     &llmController, [&llmController, &bootstrap] {
                         llmController.setLanguage(
                             bootstrap.config().general.language);
                     });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] {
            JARVIS_LOG_CRITICAL(kCategory, "QML root object could not be created");
            QCoreApplication::exit(2);
        },
        Qt::QueuedConnection);

    engine.loadFromModule("Jarvis.Ui", "Main");

    if (engine.rootObjects().isEmpty()) {
        JARVIS_LOG_CRITICAL(kCategory, "no QML root object; aborting");
        return 2;
    }

    JARVIS_LOG_INFO(kCategory, "user interface is up");
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        QObject::connect(window, &QWindow::visibilityChanged, &systemMonitor,
            [&systemMonitor](QWindow::Visibility visibility) {
                systemMonitor.setActive(visibility != QWindow::Hidden && visibility != QWindow::Minimized);
            });
    }
    if (!smokeTest) {
        QTimer::singleShot(250, &voiceController, [&] { voiceController.playGreeting(); });
        if (voiceSettings.enabled) QTimer::singleShot(2500, &voiceController, [&] { voiceController.setEnabled(true); });
    }
    if (smokeTest) {
        if(qEnvironmentVariableIsSet("JARVIS_3D_SMOKE_COMMAND")) {
            agentLoop.submit(qEnvironmentVariable("JARVIS_3D_SMOKE_COMMAND"), false);
            const auto output=qEnvironmentVariable("JARVIS_SMOKE_OUTPUT");
            QDir().mkpath(output);
            const int captureDelay=qEnvironmentVariableIsSet("JARVIS_DRAW_SMOKE")?11000:1600;
            QTimer::singleShot(captureDelay,&application,[&engine,output] {
                auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
                QCoreApplication::exit(window && window->grabWindow().save(output+"/scene.png")?0:6);
            });
        } else {
        // Offline UI smoke test: load every production page and render it.
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        const QString output = qEnvironmentVariable("JARVIS_SMOKE_OUTPUT");
        if (!window || output.isEmpty() || !QDir{}.mkpath(output)) return 6;
        if (qEnvironmentVariableIsSet("JARVIS_SMOKE_CONFIRM")) {
            toolCoordinator.submit(QStringLiteral(R"({"tool":"run_shell","arguments":{"shell":"powershell","command":"Write-Output 'Проверка окна подтверждения — команда не запускается автоматически.'"}})"));
        }
        auto* timer = new QTimer{&application};
        timer->setInterval(400);
        auto index = std::make_shared<int>(0);
        QObject::connect(timer, &QTimer::timeout, &application, [&, window, output, index, timer] {
            const QString file = output + QStringLiteral("/page-%1.png").arg(*index, 2, 10, QLatin1Char('0'));
            if (!window->grabWindow().save(file)) { QCoreApplication::exit(6); return; }
            ++*index;
            if (*index == 13) { timer->stop(); QCoreApplication::quit(); return; }
            if (!QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QVariant{*index})))
                QCoreApplication::exit(6);
        });
        timer->start();
        }
    }

    int exitCode = 0;
    try {
        exitCode = QGuiApplication::exec();
    } catch (const std::exception& ex) {
        JARVIS_LOG_CRITICAL(kCategory, "unhandled exception in event loop: {}", ex.what());
        exitCode = 3;
    } catch (...) {
        JARVIS_LOG_CRITICAL(kCategory, "unhandled non-standard exception in event loop");
        exitCode = 3;
    }

    JARVIS_LOG_INFO(kCategory, "exit code {}", exitCode);

    // Restore Qt's default handler before the logger's sinks go away.
    qInstallMessageHandler(nullptr);
    bootstrap.shutdown();

    return exitCode;
}
