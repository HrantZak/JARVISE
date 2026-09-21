import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// Asks the user about one specific action.
///
/// The dialog names the action in plain language before offering a button —
/// "JARVIS wants to open Calculator", not "Are you sure?". A confirmation the
/// user cannot decode is not consent, and this is the only path by which an
/// action with an effect on the machine ever runs.
///
/// It cannot be raised by the model. It appears when ConfirmationManager has a
/// pending request, and that only happens after a call has passed validation
/// and been found to need a human.
Item {
    id: overlay

    anchors.fill: parent
    visible: opacity > 0
    opacity: Confirm.pending ? 1 : 0
    z: 1000

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }

    // Swallows clicks so nothing behind the dialog can be operated while a
    // question is open.
    MouseArea {
        anchors.fill: parent
        enabled: Confirm.pending
        hoverEnabled: true
        onClicked: {}
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.backgroundDeep, 0.72)
    }

    Rectangle {
        id: dialog

        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacingXl * 2, 680)
        height: dialogColumn.implicitHeight + Theme.spacingXl * 2

        color: Theme.glassFillStrong
        radius: Theme.radiusMd
        border.width: Theme.borderWidth
        border.color: Theme.alpha(Theme.warning, 0.5)

        scale: Confirm.pending ? 1 : 0.96
        Behavior on scale {
            NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }

        Column {
            id: dialogColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingXl
            spacing: Theme.spacingLg

            Text {
                text: qsTr("CONFIRMATION REQUIRED")
                color: Theme.warning
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.DemiBold
            }

            // What will happen, in the user's own language.
            ScrollView {
                width: parent.width
                height: Math.min(detail.implicitHeight, overlay.height * 0.40)
                clip: true
                TextArea {
                    id: detail
                    width: parent.width
                    text: Confirm.pendingDetail
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    color: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: TextEdit.Wrap
                    background: null
                }
            }

            Text {
                width: parent.width
                text: qsTr("This will not happen unless you allow it. The assistant cannot confirm on your behalf.")
                color: Theme.textMuted
                font.family: Theme.bodyFamily
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            Row {
                width: parent.width
                spacing: Theme.spacingMd

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Confirm.pendingToolName
                    color: Theme.textDim
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontNano
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    // A real countdown from the real deadline. When it reaches
                    // zero the request lapses and nothing runs.
                    text: qsTr("%1 s").arg(Confirm.remainingSeconds)
                    color: Confirm.remainingSeconds <= 10 ? Theme.warning : Theme.textDim
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontNano
                }
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingMd

                HudButton {
                    text: qsTr("Cancel")
                    onClicked: Confirm.cancel(Confirm.pendingId)
                }

                HudButton {
                    text: qsTr("Allow")
                    primary: true
                    onClicked: Confirm.allow(Confirm.pendingId)
                }
            }
        }
    }
}
