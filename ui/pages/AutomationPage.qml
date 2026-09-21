import QtQuick
import QtCore
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

Item {
    id: page
    readonly property bool ru: App.language === "ru"
    Settings { id: saved; category: "Routines"; property string request: "" }
    PageScaffold {
        anchors.fill: parent
        title: page.ru ? "СЦЕНАРИИ" : "ROUTINES"
        subtitle: page.ru ? "Сохраните последовательность команд и запускайте её вручную. Все действия проходят обычные проверки разрешений." : "Save a sequence of commands and run it manually. Every action uses the normal permission checks."
        GlassPanel {
            width: parent.width
            height: form.implicitHeight + Theme.spacingLg * 2
            Column {
                id: form
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: Theme.spacingLg; spacing: Theme.spacingMd
                HudTextField { id: routine; width: parent.width; text: saved.request; maximumLength: 2000; placeholderText: page.ru ? "Например: проверь память, загрузку CPU и назови самые тяжёлые процессы" : "For example: check memory and CPU usage, then list the busiest processes" }
                Flow {
                    width: parent.width; spacing: Theme.spacingSm
                    HudButton { text: page.ru ? "Сохранить" : "Save"; onClicked: saved.request = routine.text.trim() }
                    HudButton { text: page.ru ? "Запустить" : "Run"; enabled: Llm.loaded && !Agent.busy && routine.text.trim().length > 0; onClicked: Agent.submit(routine.text) }
                    HudButton { text: page.ru ? "Стоп" : "Stop"; enabled: Agent.busy; onClicked: Agent.stop() }
                    HudButton { text: page.ru ? "Удалить" : "Remove"; onClicked: { saved.request = ""; routine.clear() } }
                }
                Text { width: parent.width; text: Agent.lastError.length ? Agent.lastError : Agent.agentStateLabel; color: Agent.lastError.length ? Theme.danger : Theme.textSecondary; wrapMode: Text.Wrap }
                Text { width: parent.width; text: page.ru ? "Ход работы — на странице АГЕНТ, итог — в чате. Не записывайте в сценарий пароли и API-ключи." : "Follow progress on AGENT and see the result in chat. Do not store passwords or API keys in a routine."; color: Theme.textSecondary; wrapMode: Text.Wrap }
            }
        }
    }
}
