import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Components

/// The page shown for a destination whose backend does not exist yet.
///
/// This is the honest alternative to a mock-up. It states plainly that nothing
/// is available, names the phase that will deliver it, and lists what the page
/// will actually do - so the navigation is complete without any screen
/// pretending to work. Any genuinely known facts (a real path, a real setting)
/// are shown under FACTS, because those are not speculation.
Item {
    id: page

    property string pageTitle: ""
    property string pageSubtitle: ""
    property string phase: ""

    /// Capabilities this page will gain, as plain strings.
    property var planned: []

    /// Real, verifiable values available today: [{ label, value }].
    property var facts: []

    PageScaffold {
        anchors.fill: parent
        title: page.pageTitle
        subtitle: page.pageSubtitle
        statusText: page.phase.length > 0
                    ? qsTr("NOT AVAILABLE · %1").arg(page.phase)
                    : qsTr("NOT AVAILABLE")
        statusLevel: StatusChip.Level.Inactive

        GlassPanel {
            width: parent.width
            height: notice.implicitHeight + Theme.spacingXl * 2
            title: qsTr("STATUS")

            ColumnLayout {
                id: notice
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingXl
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("NOT AVAILABLE")
                    color: Theme.textMuted
                    font.family: Theme.displayFamily
                    font.pixelSize: Theme.fontDisplay
                    font.letterSpacing: Theme.trackingWide
                    font.weight: Font.Light
                }

                Text {
                    Layout.fillWidth: true
                    text: page.phase.length > 0
                          ? qsTr("This subsystem is delivered in %1. Nothing on this page is simulated: until the backend exists, there is nothing here to show.").arg(page.phase)
                          : qsTr("This subsystem is not implemented yet.")
                    color: Theme.textDim
                    wrapMode: Text.WordWrap
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                }
            }
        }

        GlassPanel {
            width: parent.width
            height: plannedColumn.implicitHeight + Theme.spacingLg * 2
            visible: page.planned.length > 0
            title: qsTr("PLANNED CAPABILITIES")

            Column {
                id: plannedColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                Repeater {
                    model: page.planned

                    delegate: Row {
                        id: plannedRow
                        required property string modelData
                        spacing: Theme.spacingMd

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 5
                            height: 1
                            color: Theme.alpha(Theme.accent, 0.5)
                        }

                        Text {
                            text: plannedRow.modelData
                            color: Theme.textSecondary
                            font.family: Theme.bodyFamily
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }
        }

        GlassPanel {
            width: parent.width
            height: factsColumn.implicitHeight + Theme.spacingLg * 2
            visible: page.facts.length > 0
            title: qsTr("FACTS")
            trailingLabel: qsTr("REAL VALUES")

            Column {
                id: factsColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                Repeater {
                    model: page.facts

                    delegate: InfoRow {
                        required property var modelData
                        width: factsColumn.width
                        label: modelData.label
                        value: modelData.value
                    }
                }
            }
        }
    }
}
