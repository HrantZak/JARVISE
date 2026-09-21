import QtQuick
import Jarvis.Theme

/// One reading in the system HUD.
///
/// The bar is a row of discrete segments rather than a filled progress bar:
/// it quantises the value the way an instrument does, and it is the motif that
/// ties the HUD, the gauges and the scales together.
///
/// When `available` is false the readout shows the metric as unavailable. It
/// never falls back to zero, because a zero looks like a measurement.
Item {
    id: readout

    property string label: ""
    property bool available: true
    property real percent: 0
    property string valueText: ""
    property string detailText: ""
    property string unavailableText: qsTr("N/A")
    property color tint: Theme.loadColor(percent)
    property int segments: 22
    property var history: []
    property bool showGraph: false

    implicitWidth: 132
    implicitHeight: showGraph ? 78 : 46

    Column {
        anchors.fill: parent
        spacing: Theme.spacingXs

        Row {
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: readout.label
                color: Theme.textMuted
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.DemiBold
            }

            Item {
                width: parent.width - x
                height: 1
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                anchors.baseline: detail.baseline
                text: readout.available ? readout.valueText : readout.unavailableText
                color: readout.available ? Theme.textPrimary : Theme.textDim
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSubtitle
            }

            Text {
                id: detail
                text: readout.available ? readout.detailText : ""
                color: Theme.textDim
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
            }
        }

        // Segmented bar.
        Row {
            width: parent.width
            spacing: 2

            Repeater {
                model: readout.segments

                delegate: Rectangle {
                    required property int index

                    readonly property real threshold:
                        (index + 1) / readout.segments * 100
                    readonly property bool lit:
                        readout.available && readout.percent >= threshold

                    width: Math.max(1, (readout.width - (readout.segments - 1) * 2)
                                       / readout.segments)
                    height: 4
                    radius: 0
                    color: lit ? readout.tint
                               : Theme.alpha(Theme.textDim, readout.available ? 0.28 : 0.14)

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast }
                    }
                }
            }
        }

        Sparkline {
            visible: readout.showGraph && readout.available
            width: parent.width
            height: 30
            values: readout.history
            lineColor: readout.tint
            showBaseline: false
        }
    }
}
