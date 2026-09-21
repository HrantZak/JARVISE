import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

Item {
    id: page
    readonly property bool ru: App.language === "ru"
    PageScaffold {
        anchors.fill: parent
        title: page.ru ? "ПРИЛОЖЕНИЯ" : "APPLICATIONS"
        subtitle: page.ru ? "Открывайте и закрывайте приложения через Jarvis. Разрешения и подтверждения действуют так же, как в чате." : "Open and close apps through Jarvis, with the same permissions and confirmations as chat."
        statusText: Agent.busy ? (page.ru ? "ВЫПОЛНЯЕТСЯ" : "WORKING") : (Llm.loaded ? (page.ru ? "ГОТОВ" : "READY") : (page.ru ? "НУЖЕН API-КЛЮЧ" : "API KEY REQUIRED"))
        GlassPanel {
            width: parent.width
            height: form.implicitHeight + Theme.spacingLg * 2
            Column {
                id: form
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: Theme.spacingLg; spacing: Theme.spacingMd
                HudTextField { id: appName; width: parent.width; placeholderText: page.ru ? "Название приложения, например Блокнот" : "App name, for example Notepad"; maximumLength: 150 }
                Flow {
                    width: parent.width; spacing: Theme.spacingSm
                    HudButton { text: page.ru ? "Открыть" : "Open"; enabled: !Agent.busy && appName.text.trim().length > 0; onClicked: Agent.submit((page.ru ? "Открой приложение: " : "Open application: ") + appName.text) }
                    HudButton { text: page.ru ? "Закрыть" : "Close"; enabled: Llm.loaded && !Agent.busy && appName.text.trim().length > 0; onClicked: Agent.submit((page.ru ? "Закрой приложение: " : "Close application: ") + appName.text) }
                    HudButton { text: page.ru ? "Стоп" : "Stop"; enabled: Agent.busy; onClicked: Agent.stop() }
                }
                Text { width: parent.width; text: Agent.lastError.length ? Agent.lastError : Agent.agentStateLabel; color: Agent.lastError.length ? Theme.danger : Theme.textSecondary; wrapMode: Text.Wrap }
                Text { width: parent.width; text: page.ru ? "Результат выполнения появится в чате и на странице АГЕНТ." : "Results appear in chat and on the AGENT page."; color: Theme.textSecondary; wrapMode: Text.Wrap }
            }
        }
    }
}
