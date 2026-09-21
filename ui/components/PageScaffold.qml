import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

/// Common frame for every page: heading block, tick rule, scrolling body.
///
/// Having one scaffold is what keeps eleven pages feeling like one instrument
/// rather than eleven separate screens.
Item {
    id: page

    property string title: ""
    property string subtitle: ""
    property string statusText: ""
    property int statusLevel: StatusChip.Level.Inactive

    /// Page content is declared as children and laid out in a Column.
    default property alias content: body.data

    property int contentSpacing: Theme.spacingLg

    Column {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingSm

        Row {
            width: parent.width
            spacing: Theme.spacingMd

            Text {
                anchors.bottom: parent.bottom
                text: page.title
                color: Theme.textPrimary
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontDisplay
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.Light
            }

            StatusChip {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                visible: page.statusText.length > 0
                text: page.statusText
                level: page.statusLevel
            }
        }

        Text {
            width: parent.width
            visible: page.subtitle.length > 0
            text: page.subtitle
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.bodyFamily
            font.pixelSize: Theme.fontBody
        }

        TickScale {
            width: parent.width
            tint: Theme.accent
            baseOpacity: 0.22
        }
    }

    Item {
        id: bodyHost
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingLg
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        ScrollView {
            id: scroller
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true

            Column {
                id: body
                width: scroller.availableWidth
                spacing: page.contentSpacing

                // Panel titles sit astride the top border, half of the label
                // above the panel itself. Without this the first panel's title
                // is clipped away by the scroll viewport.
                topPadding: Theme.spacingSm + 2
            }
        }
    }
}
