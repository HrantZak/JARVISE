#pragma once
#include <QObject>
#include <QVariantList>
namespace jarvis::app {
class AgentLoop;
class Bootstrap;
class MemoryController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    MemoryController(AgentLoop& agent, Bootstrap& bootstrap);
    QVariantList entries() const;
    QString error() const { return m_error; }
    Q_INVOKABLE bool setEnabled(bool enabled);
    Q_INVOKABLE bool setPersistent(bool enabled);
    Q_INVOKABLE bool remember(const QString& text);
    Q_INVOKABLE bool forget(const QString& id);
    Q_INVOKABLE bool clear();
Q_SIGNALS:
    void changed();
private:
    bool save();
    bool canEdit();
    AgentLoop& m_agent;
    Bootstrap& m_bootstrap;
    QString m_path;
    QString m_error;
};
}
