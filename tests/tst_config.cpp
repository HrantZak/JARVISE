#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "jarvis/config/AppConfig.h"
#include "jarvis/config/AppPaths.h"
#include "jarvis/config/ConfigStore.h"

namespace fs = std::filesystem;
using namespace jarvis::config;
using jarvis::core::ErrorCode;

namespace {

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

std::string readText(const fs::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream},
                       std::istreambuf_iterator<char>{}};
}

} // namespace

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void cloudSettingsRoundTripAndValidate() {
        QTemporaryDir directory;
        ConfigStore store{fs::path{directory.path().toStdWString()} / "config.json"};
        AppConfig config;
        config.llm.responseMode = "thorough";
        config.llm.jarvisPersonality = false;
        config.voice.sttThreads = 2;
        QVERIFY(store.save(config));
        const auto restored = store.loadOrDefault();
        QCOMPARE(restored.config.llm.responseMode, std::string{"thorough"});
        QVERIFY(!restored.config.llm.jarvisPersonality);
        QCOMPARE(restored.config.voice.sttThreads, 2);
        config.llm.responseMode = "unknown";
        config.voice.sttThreads = 999;
        const auto report = validate(config);
        QVERIFY(!report.clean());
        QCOMPARE(config.llm.responseMode, std::string{"balanced"});
        QCOMPARE(config.voice.sttThreads, 8);
    }
    // --- AppPaths ---------------------------------------------------------

    void pathsDeriveFromRoot();
    void pathsRejectEmptyRoot();
    void ensureDirectoriesCreatesTheTree();
    void resolveProducesAJarvisFolder();

    // --- Validation -------------------------------------------------------

    void validationLeavesGoodValuesAlone();
    void validationClampsAndExplains();
    void validationCentresHalfStoredPositions();

    // --- ConfigStore ------------------------------------------------------

    void missingFileCreatesDefaultsOnDisk();
    void saveThenLoadRoundTrips();
    void corruptFileIsReportedAsParseFailure();
    void corruptFileStillYieldsUsableDefaults();
    void nonObjectJsonIsRejected();
    void unknownLogLevelIsReportedAndDefaulted();
    void outOfRangeValuesFromDiskAreClamped();
    void saveIsAtomicAndKeepsValidJson();

    // --- Migration --------------------------------------------------------

    void aVersionFiveFileGainsTheAgentSectionWithSafeDefaults();
    void agentLimitsCannotBeRaisedByConfiguration();
    void persistentMemoryIsOffAndCannotOutliveMemoryItself();
    void localAiUsesTheCpuByDefault();
};

void TestConfig::aVersionFiveFileGainsTheAgentSectionWithSafeDefaults() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const fs::path file =
        fs::path{temporary.path().toStdWString()} / L"config.json";

    // A real v5 file: tools present, agent absent. Everything the user set must
    // survive, and the new section must arrive at its safe defaults.
    writeText(file, R"({
        "configVersion": 5,
        "general": { "language": "en" },
        "tools": { "enabled": true, "maxRounds": 3 },
        "window": { "width": 1600, "height": 900 }
    })");

    ConfigStore store{file};
    const auto result = store.load();
    QVERIFY(result.has_value());

    const AppConfig& config = result->config;

    // Nothing the user chose was lost.
    QCOMPARE(config.general.language, jarvis::i18n::Language::English);
    QCOMPARE(config.tools.maxRounds, 3);
    QCOMPARE(config.window.width, 1600);

    // The agent arrives with defaults, and the version is stamped forward.
    QCOMPARE(config.configVersion, kCurrentConfigVersion);
    QCOMPARE(config.agent.enabled, true);
    QCOMPARE(config.agent.memoryPersistent, false);
    QCOMPARE(config.agent.memoryEnabled, false);

    // The upgrade is reported rather than silent.
    QVERIFY(!result->validation.clean());
}

void TestConfig::agentLimitsCannotBeRaisedByConfiguration() {
    AppConfig config;
    config.agent.maxSteps = 100000;
    config.agent.maxRetries = 999;
    config.agent.maxToolCalls = 100000;
    config.agent.maxToolResultChars = 10'000'000;

    const ValidationReport report = validate(config);

    // A configuration file may narrow a limit. It can never widen one past the
    // ceiling in the code that owns the structure, so an agent with a thousand
    // steps cannot be configured into existence.
    QCOMPARE(config.agent.maxSteps, 32);
    QCOMPARE(config.agent.maxRetries, kMaxRetryCeiling);
    QCOMPARE(config.agent.maxToolCalls, 64);
    QCOMPARE(config.agent.maxToolResultChars, 4096);
    QVERIFY(!report.clean());

    // And narrowing is honoured.
    AppConfig narrow;
    narrow.agent.maxSteps = 3;
    static_cast<void>(validate(narrow));
    QCOMPARE(narrow.agent.maxSteps, 3);
}

void TestConfig::persistentMemoryIsOffAndCannotOutliveMemoryItself() {
    const AppConfig defaults;
    QCOMPARE(defaults.agent.memoryEnabled, false);
    QCOMPARE(defaults.agent.memoryPersistent, false);

    // Persistence without memory is a setting that cannot mean anything.
    // Honouring half of it would be worse than refusing it.
    AppConfig contradictory;
    contradictory.agent.memoryEnabled = false;
    contradictory.agent.memoryPersistent = true;

    const ValidationReport report = validate(contradictory);
    QCOMPARE(contradictory.agent.memoryPersistent, false);
    QVERIFY(!report.clean());
}

void TestConfig::localAiUsesTheCpuByDefault() {
    const AppConfig defaults;
    QCOMPARE(defaults.llm.gpuLayers, 0);
    QCOMPARE(defaults.voice.sttUseGpu, false);
}

void TestConfig::pathsDeriveFromRoot() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path root = fs::path{temporary.path().toStdWString()} / "JARVIS";

    const auto paths = AppPaths::resolveWithRoot(root);
    QVERIFY(paths.has_value());

    QCOMPARE(paths->root(), root);
    QCOMPARE(paths->logDirectory(), root / "logs");
    QCOMPARE(paths->dataDirectory(), root / "data");
    QCOMPARE(paths->modelDirectory(), root / "models");
    QCOMPARE(paths->configFile(), root / "config.json");
}

void TestConfig::pathsRejectEmptyRoot() {
    const auto paths = AppPaths::resolveWithRoot({});
    QVERIFY(!paths.has_value());
    QCOMPARE(paths.error().code(), ErrorCode::InvalidArgument);
}

void TestConfig::ensureDirectoriesCreatesTheTree() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path root = fs::path{temporary.path().toStdWString()} / "nested" / "JARVIS";

    const auto paths = AppPaths::resolveWithRoot(root);
    QVERIFY(paths.has_value());
    QVERIFY(paths->ensureDirectories().has_value());

    QVERIFY(fs::is_directory(paths->root()));
    QVERIFY(fs::is_directory(paths->logDirectory()));
    QVERIFY(fs::is_directory(paths->dataDirectory()));
    QVERIFY(fs::is_directory(paths->modelDirectory()));

    // Running it twice must be harmless.
    QVERIFY(paths->ensureDirectories().has_value());
}

void TestConfig::resolveProducesAJarvisFolder() {
    const auto paths = AppPaths::resolve();
    QVERIFY(paths.has_value());
    QCOMPARE(paths->root().filename().string(), std::string{"JARVIS"});
    QVERIFY(paths->root().is_absolute());
}

void TestConfig::validationLeavesGoodValuesAlone() {
    AppConfig config;
    const ValidationReport report = validate(config);

    QVERIFY2(report.clean(), report.adjustments.empty()
                                 ? ""
                                 : report.adjustments.front().c_str());
    QCOMPARE(config.window.width, 1280);
    QCOMPARE(config.logging.retentionDays, 14);
}

void TestConfig::validationClampsAndExplains() {
    AppConfig config;
    config.window.width = 10;
    config.window.height = 999999;
    config.logging.retentionDays = 0;
    config.logging.maxFileBytes = 1;

    const ValidationReport report = validate(config);

    QVERIFY(!report.clean());
    QCOMPARE(report.adjustments.size(), std::size_t{4});

    QCOMPARE(config.window.width, 960);
    QCOMPARE(config.window.height, 4320);
    QCOMPARE(config.logging.retentionDays, 1);
    QCOMPARE(config.logging.maxFileBytes, std::size_t{64u * 1024u});
}

void TestConfig::validationCentresHalfStoredPositions() {
    AppConfig config;
    config.window.x = 100;
    config.window.y = WindowSettings::kUnsetPosition;

    const ValidationReport report = validate(config);

    QVERIFY(!report.clean());
    QCOMPARE(config.window.x, WindowSettings::kUnsetPosition);
    QCOMPARE(config.window.y, WindowSettings::kUnsetPosition);
}

void TestConfig::missingFileCreatesDefaultsOnDisk() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path file = fs::path{temporary.path().toStdWString()} / "config.json";

    ConfigStore store{file};
    const auto loaded = store.load();

    QVERIFY(loaded.has_value());
    QVERIFY(loaded->createdDefaults);
    QVERIFY2(fs::exists(file), "a first run must leave a real config file behind");

    const std::string text = readText(file);
    QVERIFY2(text.find("\"configVersion\"") != std::string::npos, text.c_str());
    QVERIFY2(text.find("\"logging\"") != std::string::npos, text.c_str());
    QVERIFY2(text.find("\"window\"") != std::string::npos, text.c_str());
}

void TestConfig::saveThenLoadRoundTrips() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path file = fs::path{temporary.path().toStdWString()} / "config.json";

    AppConfig written;
    written.logging.level = jarvis::logging::LogLevel::Debug;
    written.logging.logToConsole = true;
    written.logging.retentionDays = 30;
    written.logging.maxFileBytes = 2u * 1024u * 1024u;
    written.window.width = 1600;
    written.window.height = 900;
    written.window.x = 120;
    written.window.y = 64;
    written.window.rememberGeometry = false;

    ConfigStore store{file};
    QVERIFY(store.save(written).has_value());

    ConfigStore reader{file};
    const auto loaded = reader.load();
    QVERIFY(loaded.has_value());
    QVERIFY(!loaded->createdDefaults);
    QVERIFY(loaded->validation.clean());

    const AppConfig& read = loaded->config;
    QCOMPARE(read.logging.level, jarvis::logging::LogLevel::Debug);
    QCOMPARE(read.logging.logToConsole, true);
    QCOMPARE(read.logging.retentionDays, 30);
    QCOMPARE(read.logging.maxFileBytes, std::size_t{2u * 1024u * 1024u});
    QCOMPARE(read.window.width, 1600);
    QCOMPARE(read.window.height, 900);
    QCOMPARE(read.window.x, 120);
    QCOMPARE(read.window.y, 64);
    QCOMPARE(read.window.rememberGeometry, false);
}

void TestConfig::corruptFileIsReportedAsParseFailure() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path file = fs::path{temporary.path().toStdWString()} / "config.json";
    writeText(file, "{ this is not json ");

    ConfigStore store{file};
    const auto loaded = store.load();

    QVERIFY(!loaded.has_value());
    QCOMPARE(loaded.error().code(), ErrorCode::ParseFailure);
}

void TestConfig::corruptFileStillYieldsUsableDefaults() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path file = fs::path{temporary.path().toStdWString()} / "config.json";
    writeText(file, "@@@ broken @@@");

    ConfigStore store{file};
    std::optional<jarvis::core::Error> error;
    const ConfigStore::LoadResult result = store.loadOrDefault(&error);

    QVERIFY2(error.has_value(), "the parse failure must still be reported");
    QCOMPARE(error->code(), ErrorCode::ParseFailure);

    // ... and the application must still get something it can run with.
    QCOMPARE(result.config.window.width, 1280);
    QCOMPARE(result.config.logging.level, jarvis::logging::LogLevel::Info);
}

void TestConfig::nonObjectJsonIsRejected() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path file = fs::path{temporary.path().toStdWString()} / "config.json";
    writeText(file, "[1, 2, 3]");

    ConfigStore store{file};
    const auto loaded = store.load();
    QVERIFY(!loaded.has_value());
    QCOMPARE(loaded.error().code(), ErrorCode::ParseFailure);
}

void TestConfig::unknownLogLevelIsReportedAndDefaulted() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path file = fs::path{temporary.path().toStdWString()} / "config.json";
    writeText(file, R"({ "configVersion": 1, "logging": { "level": "shouty" } })");

    ConfigStore store{file};
    const auto loaded = store.load();

    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->config.logging.level, jarvis::logging::LogLevel::Info);
    QVERIFY(!loaded->validation.clean());
    QVERIFY2(loaded->validation.adjustments.front().find("shouty") != std::string::npos,
             loaded->validation.adjustments.front().c_str());
}

void TestConfig::outOfRangeValuesFromDiskAreClamped() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path file = fs::path{temporary.path().toStdWString()} / "config.json";
    writeText(file, R"({
        "configVersion": 1,
        "logging": { "retentionDays": 100000 },
        "window": { "width": 1, "height": 1 }
    })");

    ConfigStore store{file};
    const auto loaded = store.load();

    QVERIFY(loaded.has_value());
    QVERIFY(!loaded->validation.clean());
    QCOMPARE(loaded->config.logging.retentionDays, 365);
    QCOMPARE(loaded->config.window.width, 960);
    QCOMPARE(loaded->config.window.height, 600);
}

void TestConfig::saveIsAtomicAndKeepsValidJson() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const fs::path file = fs::path{temporary.path().toStdWString()} / "config.json";

    ConfigStore store{file};

    AppConfig first;
    first.window.width = 1400;
    QVERIFY(store.save(first).has_value());
    const std::string afterFirst = readText(file);

    AppConfig second;
    second.window.width = 1500;
    QVERIFY(store.save(second).has_value());
    const std::string afterSecond = readText(file);

    QVERIFY(afterFirst != afterSecond);
    QVERIFY2(afterSecond.find("1500") != std::string::npos, afterSecond.c_str());

    // The temporary file QSaveFile uses must not survive a successful commit.
    int strays = 0;
    for (const fs::directory_entry& entry :
         fs::directory_iterator{file.parent_path()}) {
        if (entry.path() != file) {
            ++strays;
        }
    }
    QCOMPARE(strays, 0);
}

QTEST_GUILESS_MAIN(TestConfig)

#include "tst_config.moc"
