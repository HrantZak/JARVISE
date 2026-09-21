#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <atomic>
#include "jarvis/llm/ILLMBackend.h"

namespace jarvis::app {
class DeepSeekBackend final : public llm::ILLMBackend {
public:
    explicit DeepSeekBackend(QByteArray key, QString model,
        QUrl endpoint = QUrl{"https://api.deepseek.com/chat/completions"});
    std::string_view name() const noexcept override { return "DeepSeek API"; }
    std::vector<llm::DeviceInfo> devices() const override { return {{"DeepSeek", "Cloud"}}; }
    bool supportsGpuOffload() const noexcept override { return false; }
    core::Status load(const llm::ModelInfo&, const llm::LoadParams&) override;
    void unload() override { m_loaded = false; }
    bool isLoaded() const noexcept override { return m_loaded; }
    const llm::LoadedModel& loadedModel() const override { return m_info; }
    core::Result<llm::GenerationStats> generate(const llm::GenerationRequest&, const llm::TokenCallback&) override;
    void requestStop() noexcept override { m_stop = true; }
private:
    QByteArray m_key;
    QString m_model;
    QUrl m_endpoint;
    llm::LoadedModel m_info;
    bool m_loaded{true};
    std::atomic<bool> m_stop{false};
};
}
