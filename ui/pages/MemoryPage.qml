import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

Item {
    id: page
    readonly property bool ru: App.language === "ru"
    PageScaffold {
        anchors.fill: parent
        title: page.ru ? "ПАМЯТЬ" : "MEMORY"
        subtitle: page.ru ? "Факты, которые вы сами разрешили запомнить. Используются как контекст будущих ответов." : "Facts you choose to remember, used as context for future answers."
        statusText: String(Agent.memoryEntryCount)
        GlassPanel {
            width: parent.width
            height: form.implicitHeight + Theme.spacingLg * 2
            Column {
                id: form
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd
                ToggleSwitch {
                    width: parent.width
                    label: page.ru ? "Использовать память" : "Use memory"
                    description: page.ru ? "Выключение также отключает сохранение на диск. Записи текущего сеанса остаются до закрытия." : "Turning this off also removes persistence. Session entries remain until exit."
                    checked: Agent.memoryEnabled
                    enabled: !Agent.busy
                    onToggled: function(value) { Memory.setEnabled(value) }
                }
                ToggleSwitch {
                    width: parent.width
                    label: page.ru ? "Сохранять новые факты между запусками" : "Keep new facts between launches"
                    checked: Agent.memoryPersistent
                    enabled: Agent.memoryEnabled && !Agent.busy
                    onToggled: function(value) { Memory.setPersistent(value) }
                }
                HudTextField {
                    id: fact
                    width: parent.width
                    placeholderText: page.ru ? "Например: мой основной редактор — Visual Studio" : "For example: my main editor is Visual Studio"
                    maximumLength: 600
                    onAccepted: { if (Memory.remember(text)) clear() }
                }
                Flow {
                    width: parent.width; spacing: Theme.spacingSm
                    HudButton {
                        text: page.ru ? "Запомнить" : "Remember"
                        enabled: Agent.memoryEnabled && !Agent.busy && fact.text.trim().length > 0
                        onClicked: { if (Memory.remember(fact.text)) fact.clear() }
                    }
                    HudButton {
                        text: page.ru ? "Очистить память" : "Clear memory"
                        enabled: !Agent.busy && Agent.memoryEntryCount > 0
                        onClicked: Memory.clear()
                    }
                }
                Text { width: parent.width; text: Memory.error; color: Theme.danger; wrapMode: Text.Wrap; visible: text.length > 0 }
            }
        }
        Repeater {
            model: Memory.entries
            delegate: GlassPanel {
                required property var modelData
                width: parent.width
                height: entry.implicitHeight + Theme.spacingLg * 2
                Column {
                    id: entry
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.spacingLg; spacing: Theme.spacingSm
                    Text { width: parent.width; text: modelData.text; color: Theme.textPrimary; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.family: Theme.bodyFamily }
                    Text { text: modelData.persistent ? (page.ru ? "Сохранено на диск" : "Saved to disk") : (page.ru ? "Только этот сеанс" : "This session only"); color: Theme.textSecondary }
                    HudButton { text: page.ru ? "Забыть" : "Forget"; enabled: !Agent.busy; onClicked: Memory.forget(modelData.id) }
                }
            }
        }
    }
}
