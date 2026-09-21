#include "MemoryController.h"
#include "AgentLoop.h"
#include "Bootstrap.h"
#include <QFile>
#include <QSaveFile>
#include <QVariantMap>
#include <QRegularExpression>

namespace jarvis::app {
MemoryController::MemoryController(AgentLoop& agent, Bootstrap& bootstrap)
    : m_agent{agent}, m_bootstrap{bootstrap},
      m_path{QString::fromStdWString((bootstrap.paths().root() / "data" / "memory.txt").wstring())} {
    agent.localCommand = [this](const QString& input) -> std::optional<QString> {
        QString text = input.trimmed();
        text.remove(QRegularExpression(QStringLiteral("^(?:джарвис|жарвис|jarvis)[, :]*"), QRegularExpression::CaseInsensitiveOption));
        const auto match = QRegularExpression(QStringLiteral("^(?:запомни|remember)(?:\\s+что)?[\\s,:]+(.+)$"), QRegularExpression::CaseInsensitiveOption).match(text);
        if (match.hasMatch()) {
            if (!m_agent.memoryPersistent() && !setPersistent(true)) return m_error;
            if (!remember(match.captured(1))) return m_error;
            return QStringLiteral("Запомнил, сэр. Этот факт сохранён и будет доступен после перезапуска.");
        }
        text = text.toLower();
        text.remove(QRegularExpression(QStringLiteral("[?!.]+$")));
        if (text == QStringLiteral("что ты помнишь") || text == QStringLiteral("покажи память") || text == "show memory") {
            QStringList facts;
            for (const auto& entry : entries()) facts.append(entry.toMap().value("text").toString());
            return facts.isEmpty() ? QStringLiteral("В памяти пока нет фактов, сэр.") : QStringLiteral("Вот что я помню, сэр:\n• ") + facts.join(QStringLiteral("\n• "));
        }
        if (text == QStringLiteral("очисти память") || text == QStringLiteral("забудь всё") || text == "clear memory")
            return clear() ? QStringLiteral("Память очищена, сэр.") : m_error;
        return std::nullopt;
    };
    if (agent.memoryPersistent()) {
        QFile file{m_path};
        if (file.exists() && file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024) {
            agent.memory().deserialise(file.readAll().toStdString());
            agent.retranslate();
        }
    }
}
QVariantList MemoryController::entries() const {
    QVariantList result;
    for (const auto& entry : m_agent.memory().recall(1024))
        result.append(QVariantMap{{"id", QString::number(entry.id)},
            {"text", QString::fromStdString(entry.content)},
            {"persistent", entry.scope == agent::MemoryScope::Persistent}});
    return result;
}
bool MemoryController::canEdit() {
    m_error.clear();
    if (!m_agent.busy()) return true;
    m_error = QStringLiteral("Дождитесь окончания текущей задачи.");
    Q_EMIT changed();
    return false;
}
bool MemoryController::save() {
    if (!m_agent.memoryPersistent()) {
        if (QFile::exists(m_path) && !QFile::remove(m_path)) {
            m_error = QStringLiteral("Не удалось удалить файл памяти.");
            return false;
        }
        return true;
    }
    const auto data = QByteArray::fromStdString(m_agent.memory().serialise());
    QSaveFile file{m_path};
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        m_error = QStringLiteral("Не удалось сохранить память на диск.");
        return false;
    }
    return true;
}
bool MemoryController::setEnabled(bool enabled) {
    if (!canEdit()) return false;
    if (!enabled && m_agent.memoryPersistent() && !setPersistent(false)) return false;
    auto config = m_bootstrap.config();
    config.agent.memoryEnabled = enabled;
    if (!enabled) config.agent.memoryPersistent = false;
    const auto result = m_bootstrap.updateConfig(config);
    if (!result) { m_error = QString::fromStdString(result.error().toUserString()); Q_EMIT changed(); return false; }
    m_agent.applySettings(config.agent);
    Q_EMIT changed();
    return true;
}
bool MemoryController::setPersistent(bool enabled) {
    if (!canEdit()) return false;
    auto config = m_bootstrap.config();
    config.agent.memoryEnabled = true;
    config.agent.memoryPersistent = enabled;
    const auto previousConfig = m_bootstrap.config();
    const auto backup = m_agent.memory();
    m_agent.applySettings(config.agent);
    if (!save()) { m_agent.applySettings(previousConfig.agent); m_agent.memory() = backup; Q_EMIT changed(); return false; }
    const auto result = m_bootstrap.updateConfig(config);
    if (!result) {
        m_agent.applySettings(previousConfig.agent);
        m_agent.memory() = backup;
        save();
        m_error = QString::fromStdString(result.error().toUserString());
        Q_EMIT changed();
        return false;
    }
    Q_EMIT changed();
    return true;
}
bool MemoryController::remember(const QString& text) {
    if (!canEdit()) return false;
    const auto backup = m_agent.memory();
    const auto entry = m_agent.memory().remember(text.trimmed().toStdString(), agent::MemoryCategory::Fact,
        m_agent.memoryPersistent() ? agent::MemoryScope::Persistent : agent::MemoryScope::Session);
    if (!entry) {
        m_error = QStringLiteral("Включите память и введите короткий факт без паролей, ключей и системных инструкций.");
        Q_EMIT changed();
        return false;
    }
    const bool ok = save();
    if (!ok) m_agent.memory() = backup;
    m_agent.retranslate();
    Q_EMIT changed();
    return ok;
}
bool MemoryController::forget(const QString& id) {
    if (!canEdit()) return false;
    const auto backup = m_agent.memory();
    if (!m_agent.memory().forget(id.toULongLong())) return false;
    const bool ok = save();
    if (!ok) m_agent.memory() = backup;
    m_agent.retranslate();
    Q_EMIT changed();
    return ok;
}
bool MemoryController::clear() {
    if (!canEdit()) return false;
    const auto backup = m_agent.memory();
    m_agent.memory().clear();
    const bool ok = save();
    if (!ok) m_agent.memory() = backup;
    m_agent.retranslate();
    Q_EMIT changed();
    return ok;
}
}
