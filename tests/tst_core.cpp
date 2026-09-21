#include <QTest>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "jarvis/core/Error.h"
#include "jarvis/core/Result.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/core/Version.h"

using namespace jarvis::core;

namespace {

/// Helper that fails from a known location, so the captured source line can be
/// checked without hard-coding a line number from this file's top.
Result<int> failingOperation() {
    return fail(ErrorCode::NotFound, "no such model");
}

Result<int> succeedingOperation() {
    return 7;
}

} // namespace

class TestCore : public QObject {
    Q_OBJECT

private slots:
    // --- Error ------------------------------------------------------------

    void errorCarriesCodeAndMessage();
    void errorToStringMentionsOriginFile();
    void errorCodeNamesAreDistinct();

    // --- Result -----------------------------------------------------------

    void resultCarriesValueOnSuccess();
    void resultCarriesErrorOnFailure();
    void statusOkIsTruthy();

    // --- Version ----------------------------------------------------------

    void buildInfoIsPopulated();

    // --- ThreadPool -------------------------------------------------------

    void poolHonoursRequestedThreadCount();
    void poolDefaultsToAtLeastOneThread();
    void submitReturnsTaskResult();
    void submitForwardsArguments();
    void submitPropagatesExceptions();
    void postRunsEveryTask();
    void postRoutesExceptionsToHandler();
    void waitIdleBlocksUntilWorkIsDone();
    void shutdownIsIdempotent();
    void submitAfterShutdownBreaksThePromise();
};

void TestCore::errorCarriesCodeAndMessage() {
    const Error error{ErrorCode::PermissionDenied, "denied"};
    QCOMPARE(error.code(), ErrorCode::PermissionDenied);
    QCOMPARE(error.message(), std::string{"denied"});
}

void TestCore::errorToStringMentionsOriginFile() {
    const Error error{ErrorCode::IoFailure, "disk is full"};
    const std::string text = error.toString();

    QVERIFY2(text.find("IoFailure") != std::string::npos, text.c_str());
    QVERIFY2(text.find("disk is full") != std::string::npos, text.c_str());
    // Source coordinates: this file, with a line number.
    QVERIFY2(text.find("tst_core.cpp") != std::string::npos, text.c_str());

    // toUserString() must not leak build paths to the UI.
    const std::string userText = error.toUserString();
    QVERIFY2(userText.find("tst_core.cpp") == std::string::npos, userText.c_str());
    QVERIFY2(userText.find("disk is full") != std::string::npos, userText.c_str());
}

void TestCore::errorCodeNamesAreDistinct() {
    const std::array codes{
        ErrorCode::Unknown,           ErrorCode::InvalidArgument, ErrorCode::NotFound,
        ErrorCode::AlreadyExists,     ErrorCode::PermissionDenied, ErrorCode::IoFailure,
        ErrorCode::ParseFailure,      ErrorCode::NotImplemented,  ErrorCode::Cancelled,
        ErrorCode::Timeout,           ErrorCode::ResourceExhausted, ErrorCode::Unavailable,
        ErrorCode::InternalFailure,
    };

    std::set<std::string_view> names;
    for (const ErrorCode code : codes) {
        names.insert(toStringView(code));
    }
    QCOMPARE(names.size(), codes.size());
}

void TestCore::resultCarriesValueOnSuccess() {
    const Result<int> result = succeedingOperation();
    QVERIFY(result.has_value());
    QCOMPARE(*result, 7);
}

void TestCore::resultCarriesErrorOnFailure() {
    const Result<int> result = failingOperation();
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code(), ErrorCode::NotFound);
    QCOMPARE(result.error().message(), std::string{"no such model"});

    // fail() must capture the caller's location, not Result.h.
    QVERIFY2(result.error().file() == "tst_core.cpp",
             std::string{result.error().file()}.c_str());
}

void TestCore::statusOkIsTruthy() {
    const Status good = ok();
    QVERIFY(good.has_value());

    const Status bad = fail(ErrorCode::Timeout, "took too long");
    QVERIFY(!bad.has_value());
    QCOMPARE(bad.error().code(), ErrorCode::Timeout);
}

void TestCore::buildInfoIsPopulated() {
    const BuildInfo& info = buildInfo();

    QVERIFY(!info.version.empty());
    QCOMPARE(info.versionMajor, 0u);
    QVERIFY(!info.compiler.empty());
    QVERIFY2(info.compiler.find("MSVC") != std::string_view::npos,
             std::string{info.compiler}.c_str());
    QCOMPARE(info.cxxStandard, std::string_view{"C++23"});
    QVERIFY(!info.qtVersion.empty());

    // The build type must be a real configuration, not the header's fallback.
    QVERIFY2(info.buildType != "Unknown", std::string{info.buildType}.c_str());

    QVERIFY(!buildSummary().empty());
}

void TestCore::poolHonoursRequestedThreadCount() {
    ThreadPool pool{3};
    QCOMPARE(pool.threadCount(), std::size_t{3});
}

void TestCore::poolDefaultsToAtLeastOneThread() {
    ThreadPool pool;
    QVERIFY(pool.threadCount() >= 1);
}

void TestCore::submitReturnsTaskResult() {
    ThreadPool pool{2};
    std::future<int> future = pool.submit([] { return 42; });
    QCOMPARE(future.get(), 42);
}

void TestCore::submitForwardsArguments() {
    ThreadPool pool{2};
    std::future<std::string> future =
        pool.submit([](std::string prefix, int value) { return prefix + std::to_string(value); },
                    std::string{"n="}, 5);
    QCOMPARE(future.get(), std::string{"n=5"});
}

void TestCore::submitPropagatesExceptions() {
    ThreadPool pool{1};
    std::future<void> future =
        pool.submit([] { throw std::runtime_error{"task blew up"}; });

    bool threw = false;
    try {
        future.get();
    } catch (const std::runtime_error& ex) {
        threw = true;
        QCOMPARE(std::string{ex.what()}, std::string{"task blew up"});
    }
    QVERIFY(threw);
}

void TestCore::postRunsEveryTask() {
    ThreadPool pool{4};
    std::atomic<int> counter{0};

    constexpr int kTaskCount = 200;
    for (int i = 0; i < kTaskCount; ++i) {
        pool.post([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    pool.waitIdle();
    QCOMPARE(counter.load(), kTaskCount);
}

void TestCore::postRoutesExceptionsToHandler() {
    ThreadPool pool{1};
    std::promise<std::string> reported;
    std::future<std::string> future = reported.get_future();

    pool.setExceptionHandler([&reported](std::exception_ptr eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& ex) {
            reported.set_value(ex.what());
        }
    });

    pool.post([] { throw std::runtime_error{"posted failure"}; });

    QCOMPARE(future.wait_for(std::chrono::seconds{5}), std::future_status::ready);
    QCOMPARE(future.get(), std::string{"posted failure"});

    // The pool must survive a task that threw.
    std::future<int> healthy = pool.submit([] { return 1; });
    QCOMPARE(healthy.get(), 1);
}

void TestCore::waitIdleBlocksUntilWorkIsDone() {
    ThreadPool pool{2};
    std::atomic<int> finished{0};

    for (int i = 0; i < 8; ++i) {
        pool.post([&finished] {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
            finished.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.waitIdle();
    QCOMPARE(finished.load(), 8);
    QCOMPARE(pool.pendingTasks(), std::size_t{0});
}

void TestCore::shutdownIsIdempotent() {
    ThreadPool pool{2};
    pool.post([] {});
    pool.shutdown();
    QVERIFY(pool.isShutdown());

    pool.shutdown();  // must not hang or crash
    QVERIFY(pool.isShutdown());
}

void TestCore::submitAfterShutdownBreaksThePromise() {
    ThreadPool pool{1};
    pool.shutdown();

    std::future<int> future = pool.submit([] { return 1; });

    bool threw = false;
    try {
        future.get();
    } catch (const std::future_error&) {
        threw = true;
    }
    QVERIFY2(threw, "a dropped task must break its promise, not hang the caller");
}

QTEST_GUILESS_MAIN(TestCore)

#include "tst_core.moc"
