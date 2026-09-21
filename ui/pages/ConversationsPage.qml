import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// The live conversation with the local model.
///
/// Text streams in token by token as the model produces it. Nothing here is
/// pre-written: with no model loaded the page says so and the input is closed.
/// Stored history across sessions is a separate subsystem and arrives in
/// Phase 9 - this page holds the current conversation only.
Item {
    id: page

    // --- Heading ----------------------------------------------------------

    Column {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingSm

        Row {
            width: parent.width
            spacing: Theme.spacingMd

            Text {
                anchors.bottom: parent.bottom
                text: qsTr("CONVERSATION")
                color: Theme.textPrimary
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontTitle
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.Light
            }

            StatusChip {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                text: Llm.loaded
                      ? (Llm.generating ? (Llm.reasoning ? (App.language === "ru" ? "ГЛУБОКИЙ РАЗБОР" : "REASONING") : qsTr("GENERATING")) : qsTr("READY"))
                      : (App.language === "ru" ? "НУЖЕН API-КЛЮЧ" : "API KEY REQUIRED")
                level: Llm.loaded
                       ? (Llm.generating ? StatusChip.Level.Pending : StatusChip.Level.Ready)
                       : StatusChip.Level.Inactive
            }
        }

        Text {
            width: parent.width
            text: Llm.loaded
                  ? "DeepSeek API"
                  : (App.language === "ru" ? "Введите свой ключ на странице DEEPSEEK API." : "Enter your key on the DEEPSEEK API page.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.bodyFamily
            font.pixelSize: Theme.fontSmall
        }

        TickScale {
            width: parent.width
            tint: Theme.accent
            baseOpacity: 0.22
        }
    }

    // --- Transcript -------------------------------------------------------

    ListView {
        id: transcript
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingLg
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: composer.top
        anchors.bottomMargin: Theme.spacingLg

        clip: true
        spacing: Theme.spacingMd
        model: Llm.conversation

        // Follow the answer as it streams, but only when the user is already at
        // the bottom - yanking the view away from someone reading is rude.
        property bool atBottom: contentY >= contentHeight - height - 40

        onCountChanged: if (atBottom) positionViewAtEnd()

        Connections {
            target: Llm.conversation
            function onDataChanged() {
                if (transcript.atBottom) {
                    transcript.positionViewAtEnd()
                }
            }
        }

        delegate: Item {
            id: bubble
            required property string role
            required property string text
            required property string timestamp
            required property bool streaming

            readonly property bool fromUser: role === "user"

            width: transcript.width
            height: content.implicitHeight + Theme.spacingMd * 2

            Rectangle {
                anchors.fill: parent
                color: bubble.fromUser ? "transparent" : Theme.glassFill
                border.width: bubble.fromUser ? 0 : 1
                border.color: Theme.glassStroke
                radius: Theme.radiusSm
            }

            // Speaker marker: the same vertical rule as the Command Center.
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingMd
                width: 2
                height: 16
                color: bubble.fromUser ? Theme.textMuted : Theme.accent
            }

            Column {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                spacing: Theme.spacingXs

                Row {
                    spacing: Theme.spacingSm

                    Text {
                        text: bubble.fromUser ? qsTr("YOU") : "JARVIS"
                        color: bubble.fromUser ? Theme.textMuted : Theme.accent
                        font.family: Theme.displayFamily
                        font.pixelSize: Theme.fontNano
                        font.letterSpacing: Theme.trackingWide
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: bubble.timestamp
                        color: Theme.textDim
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontNano
                    }
                }

                TextEdit {
                    width: parent.width
                    text: bubble.text
                    color: bubble.fromUser ? Theme.textSecondary : Theme.textPrimary
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    selectionColor: Theme.alpha(Theme.accent, 0.35)
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontBody
                }

                // Caret while the answer is still arriving.
                Rectangle {
                    visible: bubble.streaming
                    width: 7
                    height: 2
                    color: Theme.accent

                    SequentialAnimation on opacity {
                        running: bubble.streaming
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.15; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }
                }
            }
        }
    }

    Text {
        anchors.centerIn: transcript
        width: transcript.width * 0.7
        visible: Llm.conversation.count === 0
        text: Llm.loaded
              ? (App.language === "ru" ? "Задайте вопрос DeepSeek." : "Ask DeepSeek a question.")
              : (App.language === "ru" ? "Добавьте API-ключ DeepSeek." : "Add your DeepSeek API key.")
        color: Theme.textDim
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.family: Theme.bodyFamily
        font.pixelSize: Theme.fontSmall
    }

    // --- Composer ---------------------------------------------------------

    GlassPanel {
        id: composer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 88
        title: qsTr("MESSAGE")
        trailingLabel: Llm.lastStats
        trailingColor: Theme.textDim

        TextField {
            id: input
            anchors.left: parent.left
            anchors.right: buttons.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingMd

            enabled: !Agent.busy && !Llm.generating
            placeholderText: Llm.loaded ? qsTr("Type a message…")
                                        : (App.language === "ru" ? "Команда, например: открой калькулятор" : "Command, for example: open calculator")
            color: Theme.textPrimary
            placeholderTextColor: Theme.textDim
            font.family: Theme.bodyFamily
            font.pixelSize: Theme.fontBody

            background: Rectangle {
                color: "transparent"
                border.width: 1
                border.color: input.activeFocus ? Theme.alpha(Theme.accent, 0.6)
                                                : Theme.glassStroke
                radius: Theme.radiusSm

                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
            }

            onAccepted: page.submit()
        }

        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm

            HudButton {
                text: qsTr("CLEAR")
                implicitWidth: 96
                enabled: !Agent.busy && !Llm.generating && Llm.conversation.count > 0
                opacity: enabled ? 1.0 : 0.4
                onClicked: Llm.clearConversation()
            }

            HudButton {
                text: Agent.busy || Llm.generating ? qsTr("STOP") : qsTr("SEND")
                implicitWidth: 110
                primary: true
                enabled: true
                opacity: enabled ? 1.0 : 0.4
                onClicked: Agent.busy || Llm.generating ? Agent.stop() : page.submit()
            }
        }
    }

    function submit() {
        if (Agent.busy || Llm.generating || input.text.trim().length === 0) {
            return
        }
        Agent.submit(input.text)
        input.clear()
    }
}
