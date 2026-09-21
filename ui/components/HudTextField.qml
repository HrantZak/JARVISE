import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

TextField {
    id: field
    implicitHeight: 42
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    topPadding: Theme.spacingSm
    bottomPadding: Theme.spacingSm
    color: Theme.textPrimary
    placeholderTextColor: Theme.textSecondary
    font.family: Theme.bodyFamily
    font.pixelSize: Theme.fontBody
    selectByMouse: true
    Accessible.name: placeholderText
    background: Rectangle {
        color: Theme.backgroundInset
        radius: Theme.radiusSm
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.accent : Theme.glassStroke
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on border.width { NumberAnimation { duration: Theme.durationFast } }
    }
}
