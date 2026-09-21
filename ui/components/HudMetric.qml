import QtQuick
import Jarvis.Theme

/// Compact readout for the top status strip.
///
/// Label above, value in mono, a short segmented bar below. Unavailable
/// metrics dim to a dashed placeholder instead of showing a zero.
Item {
    id: metric

    property string label: ""
    property bool available: true
    property real percent: 0
    property string valueText: ""
    property string suffix: ""
    property color tint: Theme.loadColor(percent)
    property int segments: 12

    implicitWidth: 92
    implicitHeight: 40

    Column {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        spacing: 3

        Text {
            text: metric.label
            color: Theme.textDim
            font.family: Theme.displayFamily
            font.pixelSize: Theme.fontNano
            font.letterSpacing: Theme.trackingWide
            font.weight: Font.DemiBold
        }

        Row {
            spacing: 2

            Text {
                anchors.baseline: suffixText.baseline
                text: metric.available ? metric.valueText : "––"
                color: metric.available ? Theme.textPrimary : Theme.textDim
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontBody
            }

            Text {
                id: suffixText
                text: metric.available ? metric.suffix : ""
                color: Theme.textDim
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
            }
        }

        Row {
            spacing: 2

            Repeater {
                model: metric.segments

                delegate: Rectangle {
                    required property int index

                    readonly property bool lit:
                        metric.available &&
                        metric.percent >= (index + 1) / metric.segments * 100

                    width: Math.max(1, (metric.width - (metric.segments - 1) * 2)
                                       / metric.segments)
                    height: 3
                    color: lit ? metric.tint
                               : Theme.alpha(Theme.textDim, metric.available ? 0.26 : 0.12)

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast }
                    }
                }
            }
        }
    }
}
