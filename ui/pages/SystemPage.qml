import QtQuick
import QtQuick.Layouts
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// Live machine telemetry.
///
/// Every value here is a real reading taken once a second on a worker thread:
/// CPU from GetSystemTimes, memory from GlobalMemoryStatusEx, network from
/// GetIfTable2 and GPU from NVML. Graphs are QtQuick.Shapes, not a chart
/// library.
Item {
    id: page

    PageScaffold {
        anchors.fill: parent
        title: qsTr("SYSTEM")
        subtitle: qsTr("Live telemetry, sampled once a second off the interface thread.")
        statusText: qsTr("LIVE")
        statusLevel: StatusChip.Level.Ready

        // --- Hardware profile ---------------------------------------------

        GlassPanel {
            width: parent.width
            height: profileColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("HARDWARE PROFILE")

            Column {
                id: profileColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                InfoRow {
                    width: parent.width
                    label: qsTr("PROCESSOR")
                    value: Sys.cpuName + "   ·   " + Sys.cpuTopology
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("MEMORY")
                    value: Sys.ramTotalText
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("GRAPHICS")
                    value: Sys.gpuAvailable
                           ? Sys.gpuName + "   ·   " + Sys.vramTotalText + qsTr(" VRAM")
                           : Sys.gpuName
                    valueColor: Sys.gpuAvailable ? Theme.textPrimary : Theme.warning
                }
                InfoRow {
                    width: parent.width
                    visible: Sys.gpuAvailable && Sys.gpuDriver.length > 0
                    label: qsTr("DRIVER")
                    value: Sys.gpuDriver
                }
                InfoRow {
                    width: parent.width
                    visible: !Sys.gpuAvailable
                    label: qsTr("GPU STATUS")
                    value: Sys.gpuUnavailableReason
                    valueColor: Theme.warning
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("OPERATING SYSTEM")
                    value: Sys.osName + "   ·   " + Sys.osBuild
                }
            }
        }

        // --- Load graphs ---------------------------------------------------

        GridLayout {
            width: parent.width
            columns: page.width > 980 ? 2 : 1
            columnSpacing: Theme.spacingLg
            rowSpacing: Theme.spacingLg

            GlassPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                title: qsTr("PROCESSOR LOAD")
                trailingLabel: Sys.cpuAvailable ? Math.round(Sys.cpuPercent) + "%" : "––"
                trailingColor: Theme.loadColor(Sys.cpuPercent)

                Sparkline {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    values: Sys.cpuHistory
                    capacity: Sys.historyCapacity
                    lineColor: Theme.loadColor(Sys.cpuPercent)
                }
            }

            GlassPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                title: qsTr("MEMORY LOAD")
                trailingLabel: Sys.memoryAvailable ? Sys.memoryText : "––"
                trailingColor: Theme.loadColor(Sys.memoryPercent)

                Sparkline {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    values: Sys.memoryHistory
                    capacity: Sys.historyCapacity
                    lineColor: Theme.loadColor(Sys.memoryPercent)
                }
            }

            GlassPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                title: qsTr("GRAPHICS LOAD")
                trailingLabel: Sys.gpuAvailable ? Math.round(Sys.gpuPercent) + "%"
                                                : qsTr("UNAVAILABLE")
                trailingColor: Sys.gpuAvailable ? Theme.loadColor(Sys.gpuPercent) : Theme.textDim

                Sparkline {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    visible: Sys.gpuAvailable
                    values: Sys.gpuHistory
                    capacity: Sys.historyCapacity
                    lineColor: Theme.loadColor(Sys.gpuPercent)
                }

                Text {
                    anchors.centerIn: parent
                    visible: !Sys.gpuAvailable
                    text: qsTr("NO NVML-CAPABLE GPU")
                    color: Theme.textDim
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMicro
                    font.letterSpacing: Theme.trackingLabel
                }
            }

            GlassPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                title: qsTr("VIDEO MEMORY")
                trailingLabel: Sys.gpuAvailable ? Sys.vramText : qsTr("UNAVAILABLE")
                trailingColor: Sys.gpuAvailable ? Theme.loadColor(Sys.vramPercent) : Theme.textDim

                Sparkline {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    visible: Sys.gpuAvailable
                    values: Sys.vramHistory
                    capacity: Sys.historyCapacity
                    lineColor: Theme.loadColor(Sys.vramPercent)
                }

                Text {
                    anchors.centerIn: parent
                    visible: !Sys.gpuAvailable
                    text: qsTr("NO NVML-CAPABLE GPU")
                    color: Theme.textDim
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMicro
                    font.letterSpacing: Theme.trackingLabel
                }
            }
        }

        // --- Network -------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: 168
            title: qsTr("NETWORK THROUGHPUT")
            trailingLabel: Sys.networkAvailable
                           ? "▼ " + Sys.networkDownText + "   ▲ " + Sys.networkUpText
                           : "––"
            trailingColor: Theme.textMuted

            Sparkline {
                anchors.fill: parent
                anchors.margins: Theme.spacingLg
                values: Sys.networkHistory
                capacity: Sys.historyCapacity
                lineColor: Theme.accent
            }

            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingSm
                text: qsTr("scaled to the busiest second observed in this session")
                color: Theme.textDim
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
            }
        }
    }
}
