#include "DeepSeekBackend.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <utility>

namespace jarvis::app {
DeepSeekBackend::DeepSeekBackend(QByteArray key, QString model, QUrl endpoint)
    : m_key{std::move(key)}, m_model{std::move(model)}, m_endpoint{std::move(endpoint)} {
    m_info.info.name = m_model.toStdString();
    m_info.contextLength = 32768;
    m_info.backendName = "DeepSeek API";
    m_info.deviceName = "DeepSeek";
}

core::Status DeepSeekBackend::load(const llm::ModelInfo&, const llm::LoadParams&) {
    return core::fail(core::ErrorCode::InvalidArgument,
        "DeepSeek API mode does not use local model files.");
}

core::Result<llm::GenerationStats> DeepSeekBackend::generate(
    const llm::GenerationRequest& input, const llm::TokenCallback& onToken) {
    if (!m_loaded || m_key.isEmpty())
        return std::unexpected{core::Error{core::ErrorCode::Unavailable, "DeepSeek API key is missing or backend is unloaded."}};
    m_stop = false;
    QJsonArray messages;
    for (const auto& message : input.messages) {
        messages.append(QJsonObject{
            {"role", QString::fromUtf8(llm::roleName(message.role).data(),
                static_cast<qsizetype>(llm::roleName(message.role).size()))},
            {"content", QString::fromStdString(message.content)}});
    }
    QJsonObject body{{"model", m_model}, {"messages", messages}, {"stream", true},
        {"stream_options", QJsonObject{{"include_usage", true}}},
        {"thinking", QJsonObject{{"type", input.reasoning ? "enabled" : "disabled"}}},
        {"temperature", input.sampling.temperature}, {"top_p", input.sampling.topP}};
    if (input.sampling.maxTokens > 0) body.insert("max_tokens", input.sampling.maxTokens);
    if (input.reasoning) {
        body.insert("reasoning_effort", "high");
        body.remove("temperature");
        body.remove("top_p");
    }

    // Created on the worker thread: all network objects stay on their owning thread.
    QNetworkAccessManager network;
    QNetworkRequest request{m_endpoint};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_key);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = network.post(request, QJsonDocument{body}.toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer poll, deadline;
    QElapsedTimer elapsed;
    elapsed.start();
    llm::GenerationStats stats;
    QByteArray pending;
    bool malformed = false, gotText = false, timedOut = false, done = false;
    auto consume = [&] {
        pending += reply->readAll();
        if (pending.size() > 1024 * 1024) { malformed = true; reply->abort(); return; }
        while (pending.contains('\n')) {
            const auto end = pending.indexOf('\n');
            const auto line = pending.left(end).trimmed();
            pending.remove(0, end + 1);
            if (!line.startsWith("data:")) continue;
            if (line.mid(5).trimmed() == "[DONE]") { done = true; loop.quit(); continue; }
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(line.mid(5).trimmed(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) { malformed = true; continue; }
            const auto object = doc.object();
            if (object.contains("error")) { malformed = true; continue; }
            const auto usage = object.value("usage").toObject();
            if (usage.contains("prompt_tokens")) stats.promptTokens = usage.value("prompt_tokens").toInt();
            if (usage.contains("completion_tokens")) stats.generatedTokens = usage.value("completion_tokens").toInt();
            const auto choices = object.value("choices").toArray();
            if (choices.isEmpty()) continue;
            const auto choice = choices.first().toObject();
            if (choice.value("finish_reason").isString())
                stats.truncated = choice.value("finish_reason").toString() == "length";
            const auto text = choice.value("delta").toObject().value("content").toString().toUtf8();
            if (text.isEmpty() || m_stop) continue;
            gotText = true;
            if (!onToken(std::string_view{text.constData(), static_cast<size_t>(text.size())})) m_stop = true;
        }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (m_stop) reply->abort(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { timedOut = true; reply->abort(); });
    poll.start(25);
    deadline.setSingleShot(true);
    deadline.start(input.reasoning ? 180000 : 60000);
    loop.exec();
    consume();
    stats.generateMs = static_cast<double>(elapsed.elapsed());
    stats.cancelled = m_stop;
    if (stats.cancelled) return stats;
    if (timedOut) return std::unexpected{core::Error{core::ErrorCode::Timeout, "DeepSeek request timed out. Try again."}};
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status != 200)
        return std::unexpected{core::Error{core::ErrorCode::Unavailable,
            "DeepSeek request failed (HTTP " + std::to_string(status) + "). Check the API key, model, quota and internet connection."}};
    if (malformed || !gotText || !done)
        return std::unexpected{core::Error{core::ErrorCode::ParseFailure, "DeepSeek returned no usable text or an invalid response."}};
    return stats;
}
}
