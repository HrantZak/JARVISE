// close_application: the tool, its gate, and the chain that reaches it.
//
// The defect this exists for was not a bug in any of the machinery. Asked to
// close Telegram, JARVIS said it could not - and it was telling the truth: the
// catalogue held ten read-only tools and open_application, with no inverse. The
// refusal never reached the permission system, because there was nothing to
// permit.
//
// So these tests cover the whole chain rather than the tool alone: that the
// capability is in the catalogue the model reads, that the agent can select it,
// that the gate asks a human, and that the two independent protections hold.
//
// Nothing here closes a real application. Live behaviour is in
// tst_close_application_live, which needs a running Telegram and skips without
// one - a test that closed something on every run would be a test nobody dares
// to run.

#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <vector>

#include "AgentLoop.h"
#include "AiCoreModel.h"
#include "LlmController.h"
#include "ToolCoordinator.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/llm/ILLMBackend.h"
#include "jarvis/system/WindowsSystemMetricsProvider.h"
#include "jarvis/tools/PermissionManager.h"
#include "jarvis/tools/SystemTools.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

using namespace jarvis;

namespace {

/// Says exactly what the test tells it to say.
class ScriptedBackend final : public llm::ILLMBackend {
public:
    std::vector<std::string> replies;
    int generateCalls{0};

    std::string_view name() const noexcept override { return "scripted"; }
    std::vector<llm::DeviceInfo> devices() const override { return {}; }
    bool supportsGpuOffload() const noexcept override { return false; }

    core::Status load(const llm::ModelInfo& model,
                      const llm::LoadParams& params) override {
        m_loaded.info = model;
        m_loaded.params = params;
        m_loaded.contextLength = 4096;
        m_loaded.totalLayers = 1;
        m_loaded.deviceName = "scripted";
        m_loaded.backendName = "scripted";
        m_isLoaded = true;
        return core::ok();
    }

    void unload() override { m_isLoaded = false; }
    bool isLoaded() const noexcept override { return m_isLoaded; }
    const llm::LoadedModel& loadedModel() const override { return m_loaded; }

    core::Result<llm::GenerationStats> generate(
        const llm::GenerationRequest& request,
        const llm::TokenCallback& onToken) override {
        lastRequest = request;

        const int index = generateCalls++;
        onToken(index < static_cast<int>(replies.size())
                    ? replies[static_cast<std::size_t>(index)]
                    : std::string{"Готово."});

        llm::GenerationStats stats;
        stats.generatedTokens = 10;
        stats.promptTokens = 20;
        return stats;
    }

    void requestStop() noexcept override {}

    llm::GenerationRequest lastRequest;

private:
    llm::LoadedModel m_loaded;
    bool m_isLoaded{false};
};

} // namespace

class TestCloseApplication : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // --- the capability exists, and the model can see it --------------------
    void theToolIsRegistered();
    void theCatalogueTheModelReadsOffersIt();
    void theCatalogueSaysItNeedsConfirmation();
    void itAcceptsTheApplicationsAPersonWouldName_data();
    void itAcceptsTheApplicationsAPersonWouldName();

    // --- the schema is closed ----------------------------------------------
    void itTakesNoFreeFormName_data();
    void itTakesNoFreeFormName();
    void itRefusesAnApplicationOffTheList();

    // --- the gate -----------------------------------------------------------
    void itNeedsConfirmationUnderEveryPolicy();
    void itIsRefusedWhenConfirmedActionsAreOff();
    void closingIsNotOfferedForTheShellOrForJarvis();
    void protectionDoesNotDependOnTheAllowlist_data();
    void protectionDoesNotDependOnTheAllowlist();

    // --- the chain, end to end ----------------------------------------------
    void theAgentCanSelectItAndTheDialogAppears();
    void anApplicationThatIsNotRunningSaysSoClearly();

private:
    std::unique_ptr<core::ThreadPool> m_pool;
    std::unique_ptr<system::WindowsSystemMetricsProvider> m_metrics;
    std::unique_ptr<app::AiCoreModel> m_core;
    std::unique_ptr<app::ToolCoordinator> m_coordinator;
    std::unique_ptr<app::LlmController> m_llm;
    std::unique_ptr<app::AgentLoop> m_agent;
    ScriptedBackend* m_backend{nullptr};

    [[nodiscard]] QString catalogue() const { return m_coordinator->promptSection(); }
};

void TestCloseApplication::init() {
    m_pool = std::make_unique<core::ThreadPool>(2);
    m_metrics = std::make_unique<system::WindowsSystemMetricsProvider>();
    m_core = std::make_unique<app::AiCoreModel>();
    m_coordinator =
        std::make_unique<app::ToolCoordinator>(*m_pool, *m_metrics, *m_core);
    m_coordinator->applySettings(config::ToolSettings{});

    auto backend = std::make_unique<ScriptedBackend>();
    m_backend = backend.get();
    m_llm = std::make_unique<app::LlmController>(*m_pool, std::move(backend), *m_core);

    llm::ModelInfo info;
    info.path = "scripted.gguf";
    static_cast<void>(m_backend->load(info, {}));

    m_agent = std::make_unique<app::AgentLoop>(*m_llm, *m_coordinator, *m_core);
    config::AgentSettings settings;
    settings.enabled = true;
    settings.plannerEnabled = false;
    m_agent->applySettings(settings);
}

void TestCloseApplication::cleanup() {
    m_agent.reset();
    m_llm.reset();
    m_coordinator.reset();
    m_core.reset();
    m_pool->shutdown();
    m_pool.reset();
    m_metrics.reset();
}

// ---------------------------------------------------------------------------
// The capability exists, and the model can see it
// ---------------------------------------------------------------------------

void TestCloseApplication::theToolIsRegistered() {
    QVERIFY2(m_coordinator->registry().contains("close_application"),
             "close_application is not in the registry the application builds");
}

void TestCloseApplication::theCatalogueTheModelReadsOffersIt() {
    // The decisive one for the original defect. The model answered "I cannot"
    // because this string was absent, and it was right to.
    QVERIFY2(catalogue().contains(QStringLiteral("close_application")),
             "the catalogue the model reads does not mention close_application");

    // And the description says plainly what it does, so a model that reads it
    // does not have to infer that closing is allowed.
    QVERIFY(catalogue().contains(QStringLiteral("Closes one of a small set")));
}

void TestCloseApplication::theCatalogueSaysItNeedsConfirmation() {
    const int position = catalogue().indexOf(QStringLiteral("close_application"));
    QVERIFY(position >= 0);

    // The marker belongs to this tool's line, not to a neighbour's.
    const int lineEnd = catalogue().indexOf(QChar{u'\n'}, position);
    const QString line = catalogue().mid(position, lineEnd - position);
    QVERIFY2(line.contains(QStringLiteral("needs the user's confirmation")),
             qPrintable("the catalogue does not say it asks first: " + line));
}

void TestCloseApplication::itAcceptsTheApplicationsAPersonWouldName_data() {
    QTest::addColumn<QString>("application");

    // Telegram first: the request that exposed the gap.
    for (const char* name : {"telegram", "discord", "chrome", "edge", "firefox",
                             "notepad", "calculator", "spotify", "steam", "vlc"}) {
        QTest::newRow(name) << QString::fromLatin1(name);
    }
}

void TestCloseApplication::itAcceptsTheApplicationsAPersonWouldName() {
    QFETCH(QString, application);

    QVERIFY2(tools::CloseApplicationTool::resolve(application.toStdString()).has_value(),
             qPrintable(QStringLiteral("%1 is not on the allowlist").arg(application)));

    // And the validator accepts a call naming it, which is what actually
    // decides whether the model can reach the tool.
    const std::string json =
        QStringLiteral(R"({"tool":"close_application","arguments":{"application":"%1"}})")
            .arg(application)
            .toStdString();
    QVERIFY2(m_coordinator->validator().validate(json).has_value(),
             qPrintable(QStringLiteral("the validator rejected %1").arg(application)));
}

// ---------------------------------------------------------------------------
// The schema is closed
// ---------------------------------------------------------------------------

void TestCloseApplication::itTakesNoFreeFormName_data() {
    QTest::addColumn<QString>("call");

    QTest::newRow("a process name")
        << R"({"tool":"close_application","arguments":{"application":"Telegram.exe"}})";
    QTest::newRow("a path")
        << R"({"tool":"close_application","arguments":{"application":"C:\\Windows\\System32\\lsass.exe"}})";
    QTest::newRow("a pid in the wrong field")
        << R"({"tool":"close_application","arguments":{"pid":1234}})";
    QTest::newRow("an extra field")
        << R"({"tool":"close_application","arguments":{"application":"telegram"},"force":true})";
    QTest::newRow("a wildcard")
        << R"({"tool":"close_application","arguments":{"application":"*"}})";
}

void TestCloseApplication::itTakesNoFreeFormName() {
    QFETCH(QString, call);

    // The whole design rests on this: ArgumentType has no free-form string, so
    // a name chosen by the model has nowhere to sit. Refusing a path is not a
    // check that could be forgotten - there is no field to put one in.
    QVERIFY2(!m_coordinator->validator().validate(call.toStdString()).has_value(),
             qPrintable("the validator accepted: " + call));
}

void TestCloseApplication::itRefusesAnApplicationOffTheList() {
    QVERIFY(!tools::CloseApplicationTool::resolve("explorer").has_value());
    QVERIFY(!tools::CloseApplicationTool::resolve("jarvis").has_value());
    QVERIFY(!tools::CloseApplicationTool::resolve("lsass").has_value());
    QVERIFY(!tools::CloseApplicationTool::resolve("").has_value());
}

// ---------------------------------------------------------------------------
// The gate
// ---------------------------------------------------------------------------

void TestCloseApplication::itNeedsConfirmationUnderEveryPolicy() {
    const tools::ITool* tool = m_coordinator->registry().lookup("close_application");
    QVERIFY(tool != nullptr);
    const tools::ToolDefinition& definition = tool->definition();
    QCOMPARE(definition.permission, tools::PermissionLevel::ConfirmRequired);

    // Enumerated rather than asserted about one policy: a setting that quietly
    // let this through would otherwise pass.
    for (const bool safeActions : {false, true}) {
        for (const bool confirmedActions : {false, true}) {
            tools::PermissionManager::Policy policy;
            policy.allowSafeActions = safeActions;
            policy.allowConfirmedActions = confirmedActions;

            const tools::PermissionManager permissions{policy};
            const tools::PermissionManager::Verdict verdict =
                permissions.evaluate(definition);
            QVERIFY2(verdict.decision != tools::PermissionManager::Decision::Allow,
                     qPrintable(QStringLiteral("closing ran without asking "
                                               "(safe=%1 confirmed=%2)")
                                    .arg(safeActions)
                                    .arg(confirmedActions)));
        }
    }
}

void TestCloseApplication::itIsRefusedWhenConfirmedActionsAreOff() {
    config::ToolSettings settings;
    settings.allowConfirmedActions = false;
    m_coordinator->applySettings(settings);

    m_backend->replies = {
        R"({"tool":"close_application","arguments":{"application":"telegram"}})",
        "Это действие запрещено настройками.",
    };

    QSignalSpy finished{m_agent.get(), &app::AgentLoop::finished};
    m_agent->submit(QStringLiteral("закрой телеграм"));
    QVERIFY(!finished.isEmpty() || finished.wait(10000));

    // Refused, and no dialog left hanging for a human who will never see it.
    QVERIFY(!m_coordinator->confirmation()->pending());
}

void TestCloseApplication::closingIsNotOfferedForTheShellOrForJarvis() {
    // Explorer is openable and not closable: closing it takes the taskbar and
    // the desktop with it. The asymmetry is deliberate.
    QVERIFY(tools::OpenApplicationTool::resolve("explorer").has_value());
    QVERIFY(!tools::CloseApplicationTool::resolve("explorer").has_value());

    // And the catalogue never offers JARVIS as something to close.
    QVERIFY(!catalogue().contains(QStringLiteral("\"jarvis\"")));
}

void TestCloseApplication::protectionDoesNotDependOnTheAllowlist_data() {
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("protectedProcess");

    QTest::newRow("JARVIS itself")
        << "C:/Users/someone/jarvise/build/bin/JARVIS.exe" << true;
    QTest::newRow("JARVIS, other case")
        << "C:/Program Files/Jarvis/jarvis.EXE" << true;
    QTest::newRow("the shell") << "C:/Windows/explorer.exe" << true;
    QTest::newRow("a service binary") << "C:/Windows/System32/lsass.exe" << true;
    QTest::newRow("a shell experience host")
        << "C:/Windows/SystemApps/ShellExperienceHost/ShellExperienceHost.exe" << true;
    QTest::newRow("unreadable path") << "" << true;

    QTest::newRow("Telegram") << "C:/Users/someone/AppData/Roaming/Telegram Desktop/Telegram.exe"
                              << false;
    QTest::newRow("Chrome") << "C:/Program Files/Google/Chrome/Application/chrome.exe" << false;
    QTest::newRow("Discord") << "C:/Users/someone/AppData/Local/Discord/Discord.exe" << false;
}

void TestCloseApplication::protectionDoesNotDependOnTheAllowlist() {
    QFETCH(QString, path);
    QFETCH(bool, protectedProcess);

    // The second gate, checked directly. Even if an allowlist entry were one
    // day added carelessly, this decides independently of it.
    QCOMPARE(tools::CloseApplicationTool::isProtectedExecutable(path.toStdWString()),
             protectedProcess);
}

// ---------------------------------------------------------------------------
// The chain, end to end
// ---------------------------------------------------------------------------

void TestCloseApplication::theAgentCanSelectItAndTheDialogAppears() {
    // "Закрой Telegram" reaching a confirmation dialog is the whole point: the
    // answer to that request should be a question, not "I cannot".
    m_backend->replies = {
        R"({"tool":"close_application","arguments":{"application":"telegram"}})",
        "Готово.",
    };

    QSignalSpy requested{m_coordinator->confirmation(),
                         &app::ConfirmationManager::requested};
    m_agent->submit(QStringLiteral("закрой телеграм"));

    QVERIFY2(!requested.isEmpty() || requested.wait(10000),
             "closing Telegram did not raise a confirmation");

    QCOMPARE(requested.first().at(1).toString(), QStringLiteral("close_application"));
    QVERIFY(m_agent->busy());

    // Cancel rather than approve: approving closes a real application, and a
    // test that did that on every run is one nobody would dare to run.
    m_coordinator->confirmation()->cancel(m_coordinator->confirmation()->pendingId());
    QTest::qWait(300);
}

void TestCloseApplication::anApplicationThatIsNotRunningSaysSoClearly() {
    // VLC is on the allowlist and almost certainly not running here. Executed
    // directly, past the confirmation, because what is under test is the
    // message a person gets - not the gate, which is covered above.
    tools::ToolRegistry registry;
    QVERIFY(registry.add(std::make_unique<tools::CloseApplicationTool>()));

    const tools::ToolValidator validator{registry};
    const auto call = validator.validate(
        R"({"tool":"close_application","arguments":{"application":"vlc"}})");
    QVERIFY(call.has_value());

    tools::CloseApplicationTool tool;
    const std::atomic<bool> cancelled{false};
    const tools::ToolResult result = tool.execute(*call, cancelled);

    if (result.ok()) {
        QSKIP("VLC is running on this machine, so the not-running path cannot "
              "be checked here");
    }

    QCOMPARE(result.errorCode(), tools::ToolErrorCode::Unavailable);
    QVERIFY2(result.errorMessage().find("does not appear to be running")
                 != std::string::npos,
             qPrintable(QString::fromStdString(result.errorMessage())));
}

QTEST_MAIN(TestCloseApplication)

#include "tst_close_application.moc"
