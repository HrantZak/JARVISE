import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import Jarvis.Theme

/// Minimal window chrome: a wordmark on the left and one overflow control on
/// the right. Navigation and window actions live in that control so the shell
/// can stay as quiet as the central voice line.
Item {
    id: bar

    /// The Window this bar controls. Required - the bar drives it directly.
    required property Window window

    /// Small text shown next to the wordmark, e.g. the version.
    property string subtitle: ""

    /// Right-aligned free slot, before the window buttons.
    default property alias trailing: trailingRow.data

    /// Destinations shown in the overflow menu.
    property var menuModel: []
    property int currentIndex: 0
    signal navigated(int index)

    implicitHeight: Theme.titleBarHeight

    // Drag anywhere on the bar. startSystemMove() hands the drag to Windows,
    // which keeps snap layouts and per-monitor DPI behaviour correct; a
    // hand-rolled mouse-delta drag loses both.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        onPressed: bar.window.startSystemMove()
        onDoubleClicked: {
            bar.window.visibility = bar.window.visibility === Window.Maximized
                ? Window.Windowed
                : Window.Maximized
        }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingMd

        // Wordmark glyph: three stacked rules, the shortest on top. The same
        // measured, decreasing rhythm as the tick scales.
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Rectangle { width: 7;  height: 1; color: Theme.alpha(Theme.accent, 0.55) }
            Rectangle { width: 13; height: 1; color: Theme.accent }
            Rectangle { width: 10; height: 1; color: Theme.alpha(Theme.accent, 0.55) }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "JARVIS"
            color: Theme.textPrimary
            font.family: Theme.displayFamily
            font.pixelSize: Theme.fontBody
            font.letterSpacing: Theme.trackingWide
            font.weight: Font.DemiBold
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: bar.subtitle.length > 0
            text: bar.subtitle
            color: Theme.textDim
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontNano
        }
    }

    Row {
        id: trailingRow
        visible: false
        anchors.right: menuButton.left
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingSm
    }

    Item {
        id: menuButton
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.titleBarHeight
        height: Theme.titleBarHeight

        Rectangle {
            anchors.fill: parent
            color: hover.hovered ? Theme.alpha(Theme.accent, 0.16) : "transparent"

            Behavior on color {
                ColorAnimation { duration: Theme.durationFast }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 3

            Repeater {
                model: 3
                delegate: Rectangle {
                    required property int index
                    width: 3
                    height: 3
                    radius: 1.5
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: hover.hovered ? Theme.accentBright : Theme.textMuted
                }
            }
        }

        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: overflow.open()
        }

        Accessible.name: qsTr("MENU")
        Accessible.role: Accessible.Button
    }

    Popup {
        id: overflow
        width: 286
        height: menuContent.implicitHeight + padding * 2
        x: bar.width - width - Theme.spacingMd
        y: bar.height + Theme.spacingSm
        padding: Theme.spacingSm
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.backgroundDeep
            border.width: 1
            border.color: Theme.alpha(Theme.accent, 0.55)
        }

        contentItem: Column {
            id: menuContent
            width: overflow.availableWidth
            spacing: 2

            Text {
                text: qsTr("JARVIS MENU")
                color: Theme.accentBright
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingWide
                leftPadding: Theme.spacingSm
                topPadding: Theme.spacingXs
                bottomPadding: Theme.spacingXs
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.hairline
            }

            Repeater {
                model: bar.menuModel

                delegate: Item {
                    id: destinationEntry
                    required property int index
                    required property var modelData
                    width: menuContent.width
                    height: 34

                    Rectangle {
                        anchors.fill: parent
                        color: destinationHover.hovered
                               ? Theme.alpha(Theme.accent, 0.13)
                               : (index === bar.currentIndex
                                  ? Theme.alpha(Theme.accent, 0.07)
                                  : "transparent")
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        text: (index + 1).toString().padStart(2, "0")
                        color: index === bar.currentIndex ? Theme.accentBright : Theme.textDim
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontNano
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingLg + Theme.spacingSm
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        elide: Text.ElideRight
                        color: index === bar.currentIndex ? Theme.textPrimary : Theme.textMuted
                        font.family: Theme.displayFamily
                        font.pixelSize: Theme.fontSmall
                        font.letterSpacing: Theme.trackingLabel
                    }

                    HoverHandler {
                        id: destinationHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: {
                            overflow.close()
                            bar.navigated(destinationEntry.index)
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.hairline
            }

            Repeater {
                model: [
                    { label: qsTr("MINIMIZE"), action: "minimize" },
                    { label: qsTr("MAXIMIZE"), action: "maximize" },
                    { label: qsTr("CLOSE JARVIS"), action: "close" }
                ]

                delegate: Item {
                    id: windowEntry
                    required property var modelData
                    width: menuContent.width
                    height: 34

                    Rectangle {
                        anchors.fill: parent
                        color: windowHover.hovered
                               ? (windowEntry.modelData.action === "close"
                                  ? Theme.alpha(Theme.danger, 0.20)
                                  : Theme.alpha(Theme.accent, 0.13))
                               : "transparent"
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        text: windowEntry.modelData.label
                        color: windowEntry.modelData.action === "close"
                               ? Theme.alpha(Theme.danger, 0.85)
                               : Theme.textMuted
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontNano
                        font.letterSpacing: Theme.trackingLabel
                    }

                    HoverHandler {
                        id: windowHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: {
                            overflow.close()
                            switch (windowEntry.modelData.action) {
                            case "minimize":
                                bar.window.showMinimized()
                                break
                            case "maximize":
                                bar.window.visibility = bar.window.visibility === Window.Maximized
                                    ? Window.Windowed
                                    : Window.Maximized
                                break
                            case "close":
                                bar.window.close()
                                break
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hairline
    }
}
