import QtQuick
import Jarvis.Theme

/// A label/value pair. Values are selectable because most of them are paths or
/// identifiers a user will want to copy.
Item {
    id: row

    property string label: ""
    property string value: ""
    property bool monospace: true
    property color valueColor: Theme.textPrimary
    property int labelWidth: Theme.labelColumnWidth

    implicitHeight: Math.max(labelText.implicitHeight, valueText.implicitHeight)
    implicitWidth: labelWidth + Theme.spacingMd + valueText.implicitWidth

    Text {
        id: labelText
        width: row.labelWidth
        text: row.label
        color: Theme.textMuted
        font.family: Theme.displayFamily
        font.pixelSize: Theme.fontNano
        font.letterSpacing: Theme.trackingWide
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    TextEdit {
        id: valueText
        anchors.left: labelText.right
        anchors.leftMargin: Theme.spacingMd
        anchors.right: parent.right

        text: row.value
        color: row.valueColor
        font.family: row.monospace ? Theme.monoFamily : Theme.bodyFamily
        font.pixelSize: Theme.fontSmall

        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.WrapAnywhere
        selectionColor: Theme.alpha(Theme.accent, 0.35)
        selectedTextColor: Theme.textPrimary
    }
}
