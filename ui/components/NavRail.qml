import QtQuick
import Jarvis.Theme

/// Primary navigation.
///
/// A numbered index rather than an icon strip: JARVIS has eleven destinations,
/// and numbering them makes the system feel enumerated and technical instead of
/// like a phone's tab bar. One indicator slides between entries, which is the
/// only thing that moves.
Item {
    id: rail

    /// Entries: [{ name, status }] where status is a StatusChip.Level.
    property var model: []
    property int currentIndex: 0

    signal navigated(int index)

    implicitWidth: Theme.navWidth

    // Rail spine.
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.hairline
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingXs
        text: qsTr("NAVIGATION")
        color: Theme.textDim
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fontNano
        font.letterSpacing: Theme.trackingLabel
    }

    // The sliding indicator. It is the single moving element in the rail.
    Rectangle {
        id: indicator
        width: 2
        height: Theme.navItemHeight
        anchors.right: parent.right
        color: Theme.accent
        y: column.y + rail.currentIndex * Theme.navItemHeight
        opacity: 0.95

        Behavior on y {
            NumberAnimation {
                duration: Theme.durationNormal
                easing.type: Theme.easeEmphasis
            }
        }
    }

    // Soft bloom trailing the indicator.
    Rectangle {
        width: 46
        height: Theme.navItemHeight
        anchors.right: parent.right
        y: indicator.y
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.alpha(Theme.accent, 0.12) }
        }
    }

    Column {
        id: column
        width: parent.width
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLg + Theme.spacingSm
        spacing: 0

        Repeater {
            model: rail.model

            delegate: Item {
                id: entry
                required property int index
                required property var modelData

                readonly property bool current: index === rail.currentIndex

                width: rail.width
                height: Theme.navItemHeight

                Rectangle {
                    anchors.fill: parent
                    color: hover.hovered && !entry.current
                           ? Theme.alpha(Theme.accent, 0.045)
                           : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast }
                    }
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingLg
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingMd
                    spacing: Theme.spacingMd

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (entry.index + 1).toString().padStart(2, "0")
                        color: entry.current ? Theme.accent : Theme.textDim
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontNano

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast }
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        // Reserve the strip the pending marker lives in, so a
                        // long label elides instead of running into it.
                        width: parent.width - x - Theme.spacingMd
                        elide: Text.ElideRight
                        text: entry.modelData.name
                        color: entry.current ? Theme.textPrimary
                                             : (hover.hovered ? Theme.textSecondary
                                                              : Theme.textMuted)
                        font.family: Theme.displayFamily
                        font.pixelSize: Theme.fontSmall
                        font.letterSpacing: Theme.trackingLabel
                        font.weight: entry.current ? Font.DemiBold : Font.Normal

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast }
                        }
                    }
                }

                // A dot marks destinations whose backend does not exist yet, so
                // the rail itself tells the truth about what is behind it.
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSm + 4
                    width: 3
                    height: 3
                    radius: 1.5
                    visible: entry.modelData.pending === true
                    color: Theme.alpha(Theme.textDim, 0.9)
                }

                HoverHandler {
                    id: hover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: rail.navigated(entry.index)
                }
            }
        }
    }
}
