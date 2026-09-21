import QtQuick
import Jarvis.Theme

/// A two-position switch in the JARVIS idiom: a rail with a hard-edged travel,
/// not a rounded pill. Nothing here comes from a platform style.
Item {
    id: control

    // `enabled` is inherited from Item on purpose: redeclaring it would shadow
    // the property that actually gates input delivery to children.
    property bool checked: false
    property string label: ""
    property string description: ""

    signal toggled(bool value)

    implicitHeight: Math.max(track.height, textColumn.implicitHeight)
    implicitWidth: 260

    Column {
        id: textColumn
        anchors.left: parent.left
        anchors.right: track.left
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            width: parent.width
            text: control.label
            color: control.enabled ? Theme.textSecondary : Theme.textDim
            font.family: Theme.bodyFamily
            font.pixelSize: Theme.fontBody
        }

        Text {
            width: parent.width
            visible: control.description.length > 0
            text: control.description
            color: Theme.textDim
            wrapMode: Text.WordWrap
            font.family: Theme.bodyFamily
            font.pixelSize: Theme.fontMicro
        }
    }

    Rectangle {
        id: track
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 46
        height: 20
        radius: Theme.radiusSm
        color: control.checked ? Theme.alpha(Theme.accent, 0.16) : Theme.alpha(Theme.textDim, 0.10)
        border.width: 1
        border.color: control.checked ? Theme.alpha(Theme.accent, 0.55)
                                      : Theme.alpha(Theme.textDim, 0.36)
        opacity: control.enabled ? 1.0 : 0.45

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            id: knob
            width: 16
            height: 12
            y: (track.height - height) / 2
            x: control.checked ? track.width - width - 3 : 3
            color: control.checked ? Theme.accent : Theme.textMuted

            Behavior on x {
                NumberAnimation {
                    duration: Theme.durationNormal
                    easing.type: Theme.easeEmphasis
                }
            }
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }

        HoverHandler {
            enabled: control.enabled
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: control.enabled
            onTapped: control.toggled(!control.checked)
        }
    }
}
