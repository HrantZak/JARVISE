import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

/// Compact top navigation for the shell. The full destination list stays
/// available on narrow windows through horizontal scrolling, while the active
/// destination is kept obvious with a single blue rule and a soft bloom.
Item {
    id: tabs

    property var model: []
    property int currentIndex: 0
    signal navigated(int index)

    implicitHeight: 46

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.backgroundRaised, 0.88)
        border.color: Theme.alpha(Theme.accent, 0.20)
        border.width: 1
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        contentWidth: tabRow.implicitWidth
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: tabRow
            height: scroller.height
            spacing: 2

            Repeater {
                model: tabs.model

                delegate: Item {
                    id: tab
                    required property int index
                    required property var modelData

                    readonly property bool selected: index === tabs.currentIndex
                    readonly property string label: modelData.name

                    width: Math.max(82, labelText.implicitWidth + Theme.spacingLg * 2)
                    height: tabRow.height

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 3
                        radius: Theme.radiusSm
                        color: selected
                               ? Theme.alpha(Theme.accent, 0.13)
                               : (hover.hovered ? Theme.alpha(Theme.accent, 0.06)
                                                : "transparent")

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 1
                        height: selected ? 2 : 1
                        color: selected ? Theme.accent : "transparent"
                        opacity: selected ? 1.0 : 0.0

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast }
                        }
                    }

                    Text {
                        id: indexText
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        text: (index + 1).toString().padStart(2, "0")
                        color: selected ? Theme.accentBright : Theme.textDim
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontNano
                    }

                    Text {
                        id: labelText
                        anchors.left: indexText.right
                        anchors.leftMargin: Theme.spacingSm
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        text: tab.label
                        elide: Text.ElideRight
                        color: selected ? Theme.textPrimary
                                        : (hover.hovered ? Theme.textSecondary
                                                          : Theme.textMuted)
                        font.family: Theme.displayFamily
                        font.pixelSize: Theme.fontNano
                        font.letterSpacing: Theme.trackingLabel
                        font.weight: selected ? Font.DemiBold : Font.Normal

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast }
                        }
                    }

                    HoverHandler {
                        id: hover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: tabs.navigated(tab.index)
                    }

                    Accessible.name: tab.label
                    Accessible.role: Accessible.Tab
                    Accessible.checked: selected
                }
            }
        }
    }
}
