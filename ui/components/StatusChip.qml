import QtQuick
import Jarvis.Theme

/// Small labelled state marker: READY, OFFLINE, PHASE 3, NOT AVAILABLE.
///
/// The whole interface uses exactly this to say what is and is not working, so
/// the vocabulary stays consistent everywhere a user looks.
Item {
    id: chip

    enum Level { Ready, Pending, Warning, Danger, Inactive }

    property int level: StatusChip.Level.Inactive
    property string text: ""
    property bool showDot: true

    readonly property color tone: {
        switch (level) {
        case StatusChip.Level.Ready:    return Theme.success
        case StatusChip.Level.Pending:  return Theme.accent
        case StatusChip.Level.Warning:  return Theme.warning
        case StatusChip.Level.Danger:   return Theme.danger
        default:                        return Theme.textDim
        }
    }

    implicitWidth: row.implicitWidth + Theme.spacingSm * 2
    implicitHeight: 24

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.alpha(chip.tone, 0.09)
        border.width: 1
        border.color: Theme.alpha(chip.tone, 0.34)
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingXs + 1

        Rectangle {
            visible: chip.showDot
            anchors.verticalCenter: parent.verticalCenter
            width: 4
            height: 4
            radius: 2
            color: chip.tone
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            color: chip.tone
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontNano
            font.letterSpacing: Theme.trackingLabel
        }
    }
}
