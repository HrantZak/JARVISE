import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

Item {
    id: page
    readonly property bool ru: App.language === "ru"
    Connections {
        target: DeepSeek
        function onConnected() { apiKey.clear() }
    }
    PageScaffold {
        anchors.fill: parent
        title: "DEEPSEEK API"
        subtitle: page.ru ? "Подключите Jarvis с помощью своего API-ключа DeepSeek." : "Connect Jarvis with your DeepSeek API key."
        statusText: DeepSeek.checking ? (page.ru ? "ПРОВЕРКА" : "CHECKING") : (Llm.loaded ? (page.ru ? "КЛЮЧ СОХРАНЁН" : "KEY SAVED") : (page.ru ? "НУЖЕН КЛЮЧ" : "KEY REQUIRED"))
        statusLevel: Llm.loaded ? StatusChip.Level.Ready : StatusChip.Level.Inactive
        GlassPanel {
            width: parent.width
            height: preferences.implicitHeight + Theme.spacingLg * 2
            title: page.ru ? "ПОВЕДЕНИЕ" : "BEHAVIOUR"
            Column {
                id: preferences
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd
                OptionSelector {
                    width: parent.width
                    label: page.ru ? "Качество ответа" : "Response quality"
                    description: page.ru ? "Баланс: обычные команды выполняются быстро, сложные вопросы разбираются глубже. Можно явно выбрать скорость или подробный разбор." : "Balanced uses fast replies for everyday commands and reasoning for complex requests. Override it when needed."
                    options: [{ code: "fast", name: page.ru ? "Быстро" : "Fast" }, { code: "balanced", name: page.ru ? "Баланс" : "Balanced" }, { code: "thorough", name: page.ru ? "Подробно" : "Thorough" }]
                    current: App.responseMode
                    onSelected: function(value) { App.setResponseMode(value) }
                }
                ToggleSwitch {
                    width: parent.width
                    label: page.ru ? "Характер Jarvis" : "Jarvis personality"
                    description: page.ru ? "Спокойный тон, обращение «сэр» и короткие реплики по ситуации. Без повторения одной фразы в каждом ответе." : "A calm voice, occasional sir, and varied acknowledgements when appropriate."
                    checked: App.jarvisPersonality
                    onToggled: function(value) { App.setJarvisPersonality(value) }
                }
            }
        }
        GlassPanel {
            width: parent.width
            height: form.implicitHeight + Theme.spacingLg * 2
            title: "DEEPSEEK"
            Column {
                id: form
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd
                Text {
                    width: parent.width
                    text: page.ru ? "Ваш API-ключ" : "Your API key"
                    color: Theme.textPrimary
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSubtitle
                }
                TextField {
                    id: apiKey
                    width: parent.width
                    height: 44
                    echoMode: TextInput.Password
                    selectByMouse: true
                    enabled: !DeepSeek.checking
                    placeholderText: page.ru ? "Вставьте свой ключ DeepSeek" : "Paste your DeepSeek key"
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textSecondary
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontBody
                    Accessible.name: page.ru ? "API-ключ DeepSeek" : "DeepSeek API key"
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: Theme.backgroundInset
                        border.color: apiKey.activeFocus ? Theme.accent : Theme.glassStroke
                    }
                    onAccepted: { if (connectButton.enabled) DeepSeek.connectKey(text) }
                }
                Flow {
                    width: parent.width
                    spacing: Theme.spacingSm
                    HudButton {
                        id: connectButton
                        primary: true
                        text: DeepSeek.checking ? (page.ru ? "Проверяем…" : "Checking…") : (page.ru ? "Сохранить и подключить" : "Save and connect")
                        enabled: apiKey.text.trim().length > 0 && !DeepSeek.checking && !Llm.generating && !Llm.loading && !Agent.busy
                        onClicked: DeepSeek.connectKey(apiKey.text)
                    }
                    HudButton {
                        text: page.ru ? "Удалить ключ" : "Remove key"
                        visible: Llm.loaded
                        enabled: !DeepSeek.checking && !Llm.generating && !Llm.loading && !Agent.busy
                        onClicked: { DeepSeek.disconnectKey(); apiKey.clear() }
                    }
                }
                Text {
                    width: parent.width
                    visible: DeepSeek.error.length > 0
                    text: DeepSeek.error
                    color: Theme.danger
                    wrapMode: Text.Wrap
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontBody
                }
                Text {
                    width: parent.width
                    text: page.ru ? "Ключ хранится в защищённом хранилище Windows. При следующем запуске Jarvis подключится автоматически. Диалог и результаты команд отправляются в DeepSeek для ответа." : "Your key is saved in Windows Credential Manager for future launches. Conversation text and command results are sent to DeepSeek to generate replies."
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontBody
                }
            }
        }
    }
}
