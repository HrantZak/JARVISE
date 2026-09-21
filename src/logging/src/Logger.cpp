#include "jarvis/logging/Logger.h"

#include <atomic>
#include <thread>
#include <utility>

namespace jarvis::logging {
namespace {

/// Small, stable, per-process thread number.
///
/// std::thread::id hashes to a 19-digit value that wrecks the log's column
/// alignment and tells the reader nothing. A sequential id assigned on first
/// use is both readable and sufficient to correlate lines from one thread.
std::uint64_t currentThreadId() {
    static std::atomic<std::uint64_t> nextId{1};
    static thread_local const std::uint64_t id = nextId.fetch_add(1, std::memory_order_relaxed);
    return id;
}

std::string_view fileNameOf(const char* path) noexcept {
    if (path == nullptr) {
        return {};
    }
    const std::string_view view{path};
    const std::size_t slash = view.find_last_of("/\\");
    return slash == std::string_view::npos ? view : view.substr(slash + 1);
}

} // namespace

Logger::Logger() = default;

Logger::~Logger() {
    // Best effort: never let a failing sink throw out of a destructor.
    try {
        flush();
    } catch (...) {
    }
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::addSink(std::shared_ptr<ILogSink> sink) {
    if (!sink) {
        return;
    }
    std::lock_guard lock{m_mutex};
    m_sinks.push_back(std::move(sink));
}

void Logger::clearSinks() {
    std::vector<std::shared_ptr<ILogSink>> doomed;
    {
        std::lock_guard lock{m_mutex};
        doomed.swap(m_sinks);
    }
    // Sinks are released outside the lock so a slow destructor cannot stall
    // other threads that are logging.
    for (const auto& sink : doomed) {
        sink->flush();
    }
}

std::size_t Logger::sinkCount() const {
    std::lock_guard lock{m_mutex};
    return m_sinks.size();
}

void Logger::setLevel(LogLevel level) noexcept {
    m_level.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() const noexcept {
    return m_level.load(std::memory_order_relaxed);
}

void Logger::log(LogLevel level,
                 std::string_view category,
                 std::string message,
                 const std::source_location& location) {
    if (!isEnabled(level)) {
        return;
    }

    LogRecord record{
        .timestamp = std::chrono::system_clock::now(),
        .level = level,
        .category = category,
        .message = std::move(message),
        .threadId = currentThreadId(),
        .file = fileNameOf(location.file_name()),
        .line = location.line(),
    };

    std::lock_guard lock{m_mutex};
    for (const auto& sink : m_sinks) {
        sink->write(record);
    }
}

void Logger::flush() {
    std::lock_guard lock{m_mutex};
    for (const auto& sink : m_sinks) {
        sink->flush();
    }
}

} // namespace jarvis::logging
