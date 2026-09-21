#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QByteArray>
#include <functional>
namespace jarvis::app {
class LlmController;
class DeepSeekConnection final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit DeepSeekConnection(LlmController& llm);
    static QByteArray savedKey();
    void setBusyGuard(std::function<bool()> guard) { m_busyGuard = std::move(guard); }
    bool checking() const { return m_checking; }
    QString error() const { return m_error; }
    Q_INVOKABLE void connectKey(const QString& key);
    Q_INVOKABLE void disconnectKey();
Q_SIGNALS:
    void changed();
    void connected();
private:
    LlmController& m_llm;
    QNetworkAccessManager m_network;
    bool m_checking{false};
    QString m_error;
    std::function<bool()> m_busyGuard;
};
}
