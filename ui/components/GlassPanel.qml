import QtQuick
import Jarvis.Theme

/// The surface everything in JARVIS sits on.
///
/// A panel is not a card: no drop shadow, no filled background, no rounded
/// chrome. It is a hairline frame with a barely-there fill, corner brackets
/// cut into the corners, and a label set into the top edge - closer to an
/// instrument bezel than to a web component.
Rectangle {
    id: panel

    /// Label set into the top border. Empty hides it.
    property string title: ""

    /// Optional right-aligned label in the same border line.
    property string trailingLabel: ""
    property color trailingColor: Theme.textMuted

    property int cornerSize: 11
    property color accentColor: Theme.accent
    property bool highlighted: false

    color: highlighted ? Theme.glassFillStrong : Theme.glassFill
    radius: Theme.radiusMd
    border.width: Theme.borderWidth
    border.color: highlighted ? Theme.alpha(accentColor, 0.42) : Theme.glassStroke

    Behavior on border.color {
        ColorAnimation { duration: Theme.durationNormal }
    }
    Behavior on color {
        ColorAnimation { duration: Theme.durationNormal }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        radius: 1
        color: Theme.alpha(panel.accentColor, panel.highlighted ? 0.50 : 0.18)
    }

    // Corner brackets: two hairlines per corner. Cheaper than a Shape and
    // pixel-exact at every scale factor.
    Repeater {
        model: [
            { hx: 0, hy: 0, dx:  1, dy:  1 },
            { hx: 1, hy: 0, dx: -1, dy:  1 },
            { hx: 0, hy: 1, dx:  1, dy: -1 },
            { hx: 1, hy: 1, dx: -1, dy: -1 }
        ]

        delegate: Item {
            required property var modelData

            x: modelData.hx * panel.width
            y: modelData.hy * panel.height

            Rectangle {
                width: panel.cornerSize
                height: 1
                color: Theme.alpha(panel.accentColor, panel.highlighted ? 0.85 : 0.42)
                x: modelData.dx > 0 ? 0 : -width
                y: modelData.dy > 0 ? 0 : -1
            }

            Rectangle {
                width: 1
                height: panel.cornerSize
                color: Theme.alpha(panel.accentColor, panel.highlighted ? 0.85 : 0.42)
                x: modelData.dx > 0 ? 0 : -1
                y: modelData.dy > 0 ? 0 : -height
            }
        }
    }

    // Title, sitting in a gap punched through the top border.
    Item {
        visible: panel.title.length > 0
        x: Theme.spacingMd
        y: -height / 2
        width: titleText.implicitWidth + Theme.spacingMd
        height: titleText.implicitHeight

        Rectangle {
            anchors.fill: parent
            color: Theme.backgroundBase
        }

        Text {
            id: titleText
            anchors.centerIn: parent
            text: panel.title
            color: Theme.textMuted
            font.family: Theme.displayFamily
            font.pixelSize: Theme.fontMicro
            font.letterSpacing: Theme.trackingWide
            font.weight: Font.DemiBold
        }
    }

    Item {
        visible: panel.trailingLabel.length > 0
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingMd
        y: -height / 2
        width: trailingText.implicitWidth + Theme.spacingMd
        height: trailingText.implicitHeight

        Rectangle {
            anchors.fill: parent
            color: Theme.backgroundBase
        }

        Text {
            id: trailingText
            anchors.centerIn: parent
            text: panel.trailingLabel
            color: panel.trailingColor
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontNano
            font.letterSpacing: Theme.trackingLabel
        }
    }
}
