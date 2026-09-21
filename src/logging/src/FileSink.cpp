#include "jarvis/logging/FileSink.h"

#include <chrono>
#include <ctime>
#include <format>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace jarvis::logging {
namespace {

std::string localDateStamp() {
    const std::time_t raw = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
    if (localtime_s(&local, &raw) != 0) {
        return "00000000";
    }
    return std::format("{:04}{:02}{:02}",
                       local.tm_year + 1900,
                       local.tm_mon + 1,
                       local.tm_mday);
}

} // namespace

FileSink::FileSink(Options options)
    : m_options{std::move(options)} {}

FileSink::~FileSink() {
    flush();
}

core::Result<std::unique_ptr<FileSink>> FileSink::create(Options options) {
    using namespace jarvis::core;

    if (options.directory.empty()) {
        return fail(ErrorCode::InvalidArgument, "log directory is empty");
    }
    if (options.baseName.empty()) {
        return fail(ErrorCode::InvalidArgument, "log base name is empty");
    }
    if (options.maxFileBytes < 4096) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("maxFileBytes too small: {}", options.maxFileBytes));
    }

    std::error_code ec;
    fs::create_directories(options.directory, ec);
    if (ec && !fs::is_directory(options.directory)) {
        return fail(ErrorCode::IoFailure,
                    std::format("cannot create log directory '{}': {}",
                                options.directory.string(), ec.message()));
    }

    // std::make_unique cannot reach the private constructor.
    std::unique_ptr<FileSink> sink{new FileSink{std::move(options)}};
    sink->purgeOldFiles();
    sink->openForDate(localDateStamp());

    if (!sink->m_stream.is_open()) {
        return fail(ErrorCode::IoFailure,
                    std::format("cannot open log file '{}'", sink->m_currentFile.string()));
    }

    return sink;
}

void FileSink::openForDate(const std::string& dateStamp) noexcept {
    try {
        if (m_stream.is_open()) {
            m_stream.flush();
            m_stream.close();
        }

        m_currentDateStamp = dateStamp;
        m_currentFile = m_options.directory /
                        std::format("{}-{}.log", m_options.baseName, dateStamp);

        m_stream.open(m_currentFile, std::ios::out | std::ios::app | std::ios::binary);

        std::error_code ec;
        const auto existing = fs::file_size(m_currentFile, ec);
        m_bytesWritten = ec ? 0 : static_cast<std::size_t>(existing);
    } catch (...) {
        m_bytesWritten = 0;
    }
}

void FileSink::rotateIfNeeded() noexcept {
    if (m_bytesWritten < m_options.maxFileBytes) {
        return;
    }

    try {
        m_stream.flush();
        m_stream.close();

        // Find a free jarvis-YYYYMMDD.<n>.log slot.
        std::error_code ec;
        for (int index = 1; index < 1000; ++index) {
            const fs::path target =
                m_options.directory /
                std::format("{}-{}.{}.log", m_options.baseName, m_currentDateStamp, index);
            if (!fs::exists(target, ec)) {
                fs::rename(m_currentFile, target, ec);
                break;
            }
        }

        m_stream.open(m_currentFile, std::ios::out | std::ios::trunc | std::ios::binary);
        m_bytesWritten = 0;
    } catch (...) {
    }
}

void FileSink::purgeOldFiles() noexcept {
    if (m_options.retentionDays <= 0) {
        return;
    }

    try {
        std::error_code ec;
        if (!fs::is_directory(m_options.directory, ec)) {
            return;
        }

        const auto cutoff = std::chrono::days{m_options.retentionDays};
        const auto now = fs::file_time_type::clock::now();
        const std::string prefix = m_options.baseName + "-";

        for (const fs::directory_entry& entry :
             fs::directory_iterator{m_options.directory, ec}) {
            if (ec) {
                return;
            }
            if (!entry.is_regular_file(ec)) {
                continue;
            }

            const std::string name = entry.path().filename().string();
            if (!name.starts_with(prefix) || !name.ends_with(".log")) {
                continue;
            }

            const auto written = entry.last_write_time(ec);
            if (ec) {
                continue;
            }
            if (now - written > cutoff) {
                fs::remove(entry.path(), ec);
            }
        }
    } catch (...) {
    }
}

void FileSink::write(const LogRecord& record) noexcept {
    try {
        // A long-running session must not keep appending to yesterday's file.
        const std::string today = localDateStamp();
        if (today != m_currentDateStamp) {
            openForDate(today);
        }

        if (!m_stream.is_open()) {
            return;
        }

        std::string line = formatRecord(record);
        line.push_back('\n');

        m_stream.write(line.data(), static_cast<std::streamsize>(line.size()));
        m_bytesWritten += line.size();

        // Anything at Warning or above is flushed at once: if the process is
        // about to die, those are the lines that explain why.
        if (record.level >= LogLevel::Warning) {
            m_stream.flush();
        }

        rotateIfNeeded();
    } catch (...) {
    }
}

void FileSink::flush() noexcept {
    try {
        if (m_stream.is_open()) {
            m_stream.flush();
        }
    } catch (...) {
    }
}

} // namespace jarvis::logging
