#include "DeepSeekConnection.h"
#include "DeepSeekBackend.h"
#include "LlmController.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <qt_windows.h>
#include <wincred.h>

namespace jarvis::app {
namespace { constexpr auto target = L"JARVIS/DeepSeekApiKey"; }
DeepSeekConnection::DeepSeekConnection(LlmController& llm) : m_llm{llm} {}
QByteArray DeepSeekConnection::savedKey() {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &credential)) return {};
    QByteArray key{reinterpret_cast<const char*>(credential->CredentialBlob),
                   static_cast<qsizetype>(credential->CredentialBlobSize)};
    CredFree(credential);
    return key;
}
void DeepSeekConnection::connectKey(const QString& input) {
    if (m_checking || m_llm.generating() || m_llm.loading() || (m_busyGuard && m_busyGuard())) {
        m_error = QStringLiteral("Дождитесь завершения текущей задачи или нажмите Стоп.");
        Q_EMIT changed();
        return;
    }
    const auto key = input.trimmed().toUtf8();
    m_error.clear();
    if (key.isEmpty() || key.contains('\r') || key.contains('\n') || key.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        m_error = QStringLiteral("Введите корректный API-ключ DeepSeek.");
        Q_EMIT changed();
        return;
    }
    m_checking = true;
    Q_EMIT changed();
    QNetworkRequest request{QUrl{"https://api.deepseek.com/models"}};
    request.setRawHeader("Authorization", "Bearer " + key);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto* reply = m_network.get(request);
    QTimer::singleShot(20000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
        m_checking = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto models = QJsonDocument::fromJson(reply->readAll()).object().value("data").toArray();
        bool modelAvailable = false;
        for (const auto& model : models)
            if (model.toObject().value("id").toString() == "deepseek-v4-flash") modelAvailable = true;
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            m_error = QStringLiteral("Не удалось подключиться (HTTP %1). Проверьте ключ, доступ к DeepSeek и интернет.").arg(status);
        } else if (!modelAvailable) {
            m_error = QStringLiteral("Ключ принят, но модель DeepSeek Flash недоступна для этого аккаунта.");
        } else if (m_llm.generating() || m_llm.loading() || (m_busyGuard && m_busyGuard())) {
            m_error = QStringLiteral("Дождитесь окончания ответа и подключите ключ ещё раз.");
        } else {
            CREDENTIALW credential{};
            credential.Type = CRED_TYPE_GENERIC;
            credential.TargetName = const_cast<LPWSTR>(target);
            credential.CredentialBlobSize = static_cast<DWORD>(key.size());
            credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.constData()));
            credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
            if (!CredWriteW(&credential, 0)) {
                m_error = QStringLiteral("Windows не смогла сохранить ключ. Попробуйте ещё раз.");
            } else {
                m_llm.replaceBackend(std::make_unique<DeepSeekBackend>(key, QStringLiteral("deepseek-v4-flash")));
                Q_EMIT connected();
            }
        }
        reply->deleteLater();
        Q_EMIT changed();
    });
}
void DeepSeekConnection::disconnectKey() {
    if (m_checking || m_llm.generating() || m_llm.loading() || (m_busyGuard && m_busyGuard())) {
        m_error = QStringLiteral("Дождитесь завершения текущей задачи или нажмите Стоп.");
        Q_EMIT changed();
        return;
    }
    m_error.clear();
    if (!CredDeleteW(target, CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
        m_error = QStringLiteral("Не удалось удалить сохранённый ключ.");
    } else {
        qunsetenv("DEEPSEEK_API_KEY");
        m_llm.replaceBackend(nullptr);
    }
    Q_EMIT changed();
}
}
