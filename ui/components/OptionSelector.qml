import QtQuick
import Jarvis.Theme

/// Segmented selector: every option visible at once, one highlighted.
///
/// Used instead of a dropdown because JARVIS's option sets are short and a
/// popup would break the flat, always-legible character of the interface.
Item {
    id: control

    property string label: ""
    property string description: ""

    /// Either a list of plain strings, or a list of { code, name } objects when
    /// the stored value and the shown text differ - as they do for languages,
    /// where the value is "ru" but the label must read "Русский".
    property var options: []

    property string current: ""

    /// Plain-string options are shown upper-cased, in the console idiom. Object
    /// options keep their label verbatim, because a language endonym is a proper
    /// noun and must not be shouted or case-folded.
    property bool uppercasePlainOptions: true

    signal selected(string value)

    function optionValue(option) {
        return option !== null && typeof option === "object" ? String(option.code)
                                                             : String(option)
    }

    function optionLabel(option) {
        if (option !== null && typeof option === "object") {
            return String(option.name)
        }
        return uppercasePlainOptions ? String(option).toUpperCase() : String(option)
    }

    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: Theme.spacingSm

        Text {
            visible: control.label.length > 0
            text: control.label
            color: Theme.textSecondary
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

        Row {
            spacing: 1

            Repeater {
                model: control.options

                delegate: Rectangle {
                    id: option
                    required property var modelData

                    readonly property bool current:
                        control.optionValue(modelData) === control.current

                    width: optionLabel.implicitWidth + Theme.spacingMd * 2
                    height: 28

                    color: current ? Theme.alpha(Theme.accent, 0.14)
                                   : (hover.hovered ? Theme.alpha(Theme.accent, 0.05)
                                                    : "transparent")
                    border.width: 1
                    border.color: current ? Theme.alpha(Theme.accent, 0.55) : Theme.glassStroke

                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                    Text {
                        id: optionLabel
                        anchors.centerIn: parent
                        text: control.optionLabel(option.modelData)
                        color: option.current ? Theme.accent : Theme.textMuted
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMicro
                        font.letterSpacing: Theme.trackingLabel
                    }

                    HoverHandler {
                        id: hover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: control.selected(control.optionValue(option.modelData))
                    }
                }
            }
        }
    }
}
