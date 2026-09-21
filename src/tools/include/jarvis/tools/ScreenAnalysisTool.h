#pragma once

#include <atomic>
#include <QString>
class QImage;

#include "jarvis/tools/ITool.h"

namespace jarvis::tools {

/// Captures a single, explicit screen or foreground-window snapshot and runs
/// Windows OCR over it. The tool never watches continuously and never sends
/// the image anywhere; the OCR text is returned to the local language model
/// as tool data so it can explain code, files and mathematics visible to the
/// user.
class ScreenAnalysisTool final : public ITool {
public:
    explicit ScreenAnalysisTool(bool saveOnly = false);
    static QString recognizeImage(const QImage& image, QString& status);

    [[nodiscard]] const ToolDefinition& definition() const override {
        return m_definition;
    }

    [[nodiscard]] ToolResult execute(const ValidatedCall& call,
                                     const std::atomic<bool>& cancelled) override;

private:
    ToolDefinition m_definition;
};

} // namespace jarvis::tools
