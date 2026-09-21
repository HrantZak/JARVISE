#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "jarvis/core/Result.h"
#include "jarvis/logging/ILogSink.h"

namespace jarvis::logging {

/// Appends records to logs/jarvis-YYYYMMDD.log.
///
/// Behaviour:
///  - a new file is started when the local date changes;
///  - the current file is rolled to jarvis-YYYYMMDD.<n>.log once it exceeds
///    maxFileBytes;
///  - files older than retentionDays are deleted on construction;
///  - Warning and above are flushed immediately so a crash keeps its last
///    lines; lower levels rely on stream buffering.
class FileSink final : public ILogSink {
public:
    struct Options {
        std::filesystem::path directory;
        std::string baseName{"jarvis"};
        std::size_t maxFileBytes{8 * 1024 * 1024};
        int retentionDays{14};
    };

    /// Creates the directory if needed and opens today's file.
    /// Returns an error instead of throwing when the path is unusable.
    [[nodiscard]] static core::Result<std::unique_ptr<FileSink>> create(Options options);

    ~FileSink() override;

    void write(const LogRecord& record) noexcept override;
    void flush() noexcept override;

    [[nodiscard]] const std::filesystem::path& currentFile() const noexcept {
        return m_currentFile;
    }

private:
    explicit FileSink(Options options);

    void openForDate(const std::string& dateStamp) noexcept;
    void rotateIfNeeded() noexcept;
    void purgeOldFiles() noexcept;

    Options m_options;
    std::ofstream m_stream;
    std::filesystem::path m_currentFile;
    std::string m_currentDateStamp;
    std::size_t m_bytesWritten{0};
};

} // namespace jarvis::logging
