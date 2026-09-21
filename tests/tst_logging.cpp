#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "jarvis/logging/FileSink.h"
#include "jarvis/logging/LogLevel.h"
#include "jarvis/logging/LogRecord.h"
#include "jarvis/logging/Logger.h"

namespace fs = std::filesystem;
using namespace jarvis::logging;

namespace {

/// Sink that keeps everything in memory, for asserting on what the Logger
/// actually forwarded.
class CapturingSink final : public ILogSink {
public:
    void write(const LogRecord& record) noexcept override {
        std::lock_guard lock{m_mutex};
        m_levels.push_back(record.level);
        m_messages.push_back(record.message);
        m_categories.emplace_back(record.category);
    }

    void flush() noexcept override { m_flushCount.fetch_add(1); }

    [[nodiscard]] std::vector<std::string> messages() const {
        std::lock_guard lock{m_mutex};
        return m_messages;
    }

    [[nodiscard]] std::vector<LogLevel> levels() const {
        std::lock_guard lock{m_mutex};
        return m_levels;
    }

    [[nodiscard]] std::vector<std::string> categories() const {
        std::lock_guard lock{m_mutex};
        return m_categories;
    }

    [[nodiscard]] int flushCount() const { return m_flushCount.load(); }

private:
    mutable std::mutex m_mutex;
    std::vector<LogLevel> m_levels;
    std::vector<std::string> m_messages;
    std::vector<std::string> m_categories;
    std::atomic<int> m_flushCount{0};
};

std::string readWholeFile(const fs::path& path) {
    std::ifstream stream{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

class TestLogging : public QObject {
    Q_OBJECT

private slots:
    // --- LogLevel ---------------------------------------------------------

    void levelNamesRoundTrip();
    void levelParsingRejectsGarbage();
    void levelParsingAcceptsAliasesAndCase();

    // --- LogRecord --------------------------------------------------------

    void formattedRecordContainsTheEssentials();
    void sourceLocationOnlyAppearsForProblems();

    // --- Logger -----------------------------------------------------------

    void loggerForwardsToEverySink();
    void loggerFiltersBelowItsLevel();
    void loggerOffSuppressesEverything();
    void loggerClearSinksFlushesThem();
    void loggerIsSafeFromManyThreads();

    // --- FileSink ---------------------------------------------------------

    void fileSinkWritesLinesToDisk();
    void fileSinkRejectsUnusableOptions();
    void fileSinkRotatesOnSize();
    void fileSinkDeletesExpiredFiles();
};

void TestLogging::levelNamesRoundTrip() {
    const LogLevel levels[]{LogLevel::Trace, LogLevel::Debug,    LogLevel::Info,
                            LogLevel::Warning, LogLevel::Error,  LogLevel::Critical,
                            LogLevel::Off};

    for (const LogLevel level : levels) {
        const std::optional<LogLevel> parsed = logLevelFromString(toConfigName(level));
        QVERIFY2(parsed.has_value(), std::string{toConfigName(level)}.c_str());
        QCOMPARE(*parsed, level);

        // Padded labels line log columns up, so they must all be equal length.
        QCOMPARE(toPaddedLabel(level).size(), std::size_t{5});
    }
}

void TestLogging::levelParsingRejectsGarbage() {
    QVERIFY(!logLevelFromString("").has_value());
    QVERIFY(!logLevelFromString("verbose").has_value());
    QVERIFY(!logLevelFromString("INFOMANIAC").has_value());
}

void TestLogging::levelParsingAcceptsAliasesAndCase() {
    QCOMPARE(logLevelFromString("INFO").value(), LogLevel::Info);
    QCOMPARE(logLevelFromString("WaRnInG").value(), LogLevel::Warning);
    QCOMPARE(logLevelFromString("warn").value(), LogLevel::Warning);
    QCOMPARE(logLevelFromString("fatal").value(), LogLevel::Critical);
    QCOMPARE(logLevelFromString("none").value(), LogLevel::Off);
}

void TestLogging::formattedRecordContainsTheEssentials() {
    LogRecord record;
    record.timestamp = std::chrono::system_clock::now();
    record.level = LogLevel::Info;
    record.category = "config";
    record.message = "model loaded";
    record.threadId = 1234;
    record.file = "ConfigStore.cpp";
    record.line = 118;

    const std::string line = formatRecord(record);

    QVERIFY2(line.find("INFO") != std::string::npos, line.c_str());
    QVERIFY2(line.find("config") != std::string::npos, line.c_str());
    QVERIFY2(line.find("model loaded") != std::string::npos, line.c_str());
    QVERIFY2(line.find("1234") != std::string::npos, line.c_str());
    QVERIFY2(line.find('\n') == std::string::npos, "records must be single-line");
}

void TestLogging::sourceLocationOnlyAppearsForProblems() {
    LogRecord record;
    record.timestamp = std::chrono::system_clock::now();
    record.category = "test";
    record.message = "hello";
    record.file = "Somewhere.cpp";
    record.line = 42;

    record.level = LogLevel::Info;
    const std::string info = formatRecord(record);
    QVERIFY2(info.find("Somewhere.cpp") == std::string::npos, info.c_str());

    record.level = LogLevel::Error;
    const std::string error = formatRecord(record);
    QVERIFY2(error.find("Somewhere.cpp:42") != std::string::npos, error.c_str());
}

void TestLogging::loggerForwardsToEverySink() {
    Logger logger;
    logger.setLevel(LogLevel::Trace);

    auto first = std::make_shared<CapturingSink>();
    auto second = std::make_shared<CapturingSink>();
    logger.addSink(first);
    logger.addSink(second);
    QCOMPARE(logger.sinkCount(), std::size_t{2});

    JARVIS_LOG_TO(logger, LogLevel::Info, "unit", "value is {}", 11);

    QCOMPARE(first->messages().size(), std::size_t{1});
    QCOMPARE(first->messages().front(), std::string{"value is 11"});
    QCOMPARE(first->categories().front(), std::string{"unit"});
    QCOMPARE(second->messages().size(), std::size_t{1});
}

void TestLogging::loggerFiltersBelowItsLevel() {
    Logger logger;
    logger.setLevel(LogLevel::Warning);

    auto sink = std::make_shared<CapturingSink>();
    logger.addSink(sink);

    JARVIS_LOG_TO(logger, LogLevel::Trace, "unit", "trace");
    JARVIS_LOG_TO(logger, LogLevel::Debug, "unit", "debug");
    JARVIS_LOG_TO(logger, LogLevel::Info, "unit", "info");
    JARVIS_LOG_TO(logger, LogLevel::Warning, "unit", "warning");
    JARVIS_LOG_TO(logger, LogLevel::Error, "unit", "error");

    const std::vector<LogLevel> levels = sink->levels();
    QCOMPARE(levels.size(), std::size_t{2});
    QCOMPARE(levels[0], LogLevel::Warning);
    QCOMPARE(levels[1], LogLevel::Error);
}

void TestLogging::loggerOffSuppressesEverything() {
    Logger logger;
    logger.setLevel(LogLevel::Off);

    auto sink = std::make_shared<CapturingSink>();
    logger.addSink(sink);

    JARVIS_LOG_TO(logger, LogLevel::Critical, "unit", "should not appear");
    QCOMPARE(sink->messages().size(), std::size_t{0});
}

void TestLogging::loggerClearSinksFlushesThem() {
    Logger logger;
    auto sink = std::make_shared<CapturingSink>();
    logger.addSink(sink);

    logger.clearSinks();

    QCOMPARE(logger.sinkCount(), std::size_t{0});
    QVERIFY(sink->flushCount() >= 1);
}

void TestLogging::loggerIsSafeFromManyThreads() {
    Logger logger;
    logger.setLevel(LogLevel::Trace);

    auto sink = std::make_shared<CapturingSink>();
    logger.addSink(sink);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 250;

    std::vector<std::thread> writers;
    writers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&logger, t] {
            for (int i = 0; i < kPerThread; ++i) {
                JARVIS_LOG_TO(logger, LogLevel::Info, "race", "{}:{}", t, i);
            }
        });
    }
    for (std::thread& writer : writers) {
        writer.join();
    }

    QCOMPARE(sink->messages().size(), std::size_t{kThreads * kPerThread});
}

void TestLogging::fileSinkWritesLinesToDisk() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    FileSink::Options options;
    options.directory = fs::path{temporary.path().toStdWString()};
    options.baseName = "unit";

    auto created = FileSink::create(options);
    if (!created) {
        QFAIL(created.error().toString().c_str());
    }

    std::unique_ptr<FileSink> sink = std::move(*created);
    const fs::path file = sink->currentFile();

    LogRecord record;
    record.timestamp = std::chrono::system_clock::now();
    record.level = LogLevel::Error;  // Error flushes immediately.
    record.category = "unit";
    record.message = "written to disk";
    record.file = "tst_logging.cpp";
    record.line = 1;

    sink->write(record);
    sink->flush();

    QVERIFY(fs::exists(file));
    const std::string contents = readWholeFile(file);
    QVERIFY2(contents.find("written to disk") != std::string::npos, contents.c_str());
    QVERIFY2(contents.find("ERROR") != std::string::npos, contents.c_str());
}

void TestLogging::fileSinkRejectsUnusableOptions() {
    FileSink::Options noDirectory;
    QVERIFY(!FileSink::create(noDirectory).has_value());

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    FileSink::Options emptyName;
    emptyName.directory = fs::path{temporary.path().toStdWString()};
    emptyName.baseName.clear();
    QVERIFY(!FileSink::create(emptyName).has_value());

    FileSink::Options tinyLimit;
    tinyLimit.directory = fs::path{temporary.path().toStdWString()};
    tinyLimit.maxFileBytes = 10;
    const auto rejected = FileSink::create(tinyLimit);
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code(), jarvis::core::ErrorCode::InvalidArgument);
}

void TestLogging::fileSinkRotatesOnSize() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const fs::path directory{temporary.path().toStdWString()};

    FileSink::Options options;
    options.directory = directory;
    options.baseName = "rot";
    options.maxFileBytes = 4096;  // the smallest the sink accepts

    auto created = FileSink::create(options);
    QVERIFY(created.has_value());
    std::unique_ptr<FileSink> sink = std::move(*created);

    LogRecord record;
    record.timestamp = std::chrono::system_clock::now();
    record.level = LogLevel::Info;
    record.category = "rot";
    record.message = std::string(200, 'x');

    // Comfortably more than 4 KiB of payload.
    for (int i = 0; i < 120; ++i) {
        sink->write(record);
    }
    sink->flush();

    int rotated = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator{directory}) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("rot-") && name.ends_with(".log") &&
            name != sink->currentFile().filename().string()) {
            ++rotated;
        }
    }

    QVERIFY2(rotated >= 1, "the sink should have rolled at least one file aside");
}

void TestLogging::fileSinkDeletesExpiredFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const fs::path directory{temporary.path().toStdWString()};

    // A stale log from the distant past, plus a file the sink must leave alone.
    const fs::path stale = directory / "purge-19990101.log";
    const fs::path unrelated = directory / "notes.txt";
    {
        std::ofstream{stale} << "old\n";
        std::ofstream{unrelated} << "keep me\n";
    }
    fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours{24 * 40});

    FileSink::Options options;
    options.directory = directory;
    options.baseName = "purge";
    options.retentionDays = 14;

    auto created = FileSink::create(options);
    QVERIFY(created.has_value());

    QVERIFY2(!fs::exists(stale), "a log older than the retention window must be removed");
    QVERIFY2(fs::exists(unrelated), "files that are not JARVIS logs must be left alone");
}

QTEST_GUILESS_MAIN(TestLogging)

#include "tst_logging.moc"
