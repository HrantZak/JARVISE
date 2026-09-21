import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// What JARVIS is able to do, and what it is currently doing.
///
/// Everything on this page is the verifiable state of this build. Where a
/// capability does not exist, it says so; where one does, the value is read
/// from the object that enforces it rather than restated here.
Item {
    id: page

    PageScaffold {
        anchors.fill: parent
        title: qsTr("SECURITY")
        subtitle: qsTr("What JARVIS is able to do on this machine, and what it has actually done.")
        statusText: Tools.enabled ? qsTr("TOOLS ON") : qsTr("TOOLS OFF")
        statusLevel: Tools.enabled ? StatusChip.Level.Ready : StatusChip.Level.Inactive

        // --- Capability boundaries ------------------------------------------

        GlassPanel {
            width: parent.width
            height: boundaryColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("BOUNDARIES")

            Column {
                id: boundaryColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                InfoRow {
                    width: parent.width
                    label: qsTr("SHELL ACCESS")
                    value: qsTr("None. No tool accepts a command, a command line or a script.")
                    monospace: false
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("FILE ACCESS")
                    value: qsTr("None. No tool accepts a path, and none reads or writes user files.")
                    monospace: false
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("APPLICATIONS")
                    value: qsTr("Four, by name, each requiring your confirmation. No other program can be started.")
                    monospace: false
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("MICROPHONE")
                    value: Voice.enabled
                           ? qsTr("Open while the voice pipeline is running. Audio stays in memory and is never written to disk.")
                           : qsTr("Not opened. The voice pipeline is switched off.")
                    monospace: false
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("NETWORK")
                    value: qsTr("No outbound connection is made. Every model runs on this machine.")
                    monospace: false
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("AUTOSTART")
                    value: qsTr("No autostart entry and no persistence mechanism is created.")
                    monospace: false
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("DATA")
                    value: App.rootPath
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("LOG CONTENT")
                    value: qsTr("Identifiers and outcomes only. No user content is written to disk.")
                    monospace: false
                }
            }
        }

        // --- The trust model -------------------------------------------------

        GlassPanel {
            width: parent.width
            height: modelColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("TRUST MODEL")

            Column {
                id: modelColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                Text {
                    width: parent.width
                    text: qsTr("The language model is treated as untrusted input. Its output is parsed, checked against a fixed schema and matched to a fixed list of tools before anything happens. None of those checks consult the model's instructions, so a model that ignored every one of them would reach exactly the same boundary.")
                    color: Theme.textSecondary
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: qsTr("The most a compromised model can obtain is a read-only fact about this computer, or an action you allowed by hand.")
                    color: Theme.textPrimary
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                // The pipeline, in the order it actually runs.
                Column {
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: [
                            qsTr("model output"),
                            qsTr("strict validation"),
                            qsTr("permission check"),
                            qsTr("your confirmation, if required"),
                            qsTr("execution"),
                            qsTr("audit record")
                        ]

                        delegate: Row {
                            required property string modelData
                            required property int index
                            spacing: Theme.spacingSm

                            Text {
                                text: parent.index === 0 ? "  " : "→"
                                color: Theme.accent
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontNano
                            }

                            Text {
                                text: parent.modelData
                                color: Theme.textMuted
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontNano
                            }
                        }
                    }
                }
            }
        }

        // --- Still to come ---------------------------------------------------

        GlassPanel {
            width: parent.width
            height: plannedColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("NOT YET IMPLEMENTED")

            Column {
                id: plannedColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        qsTr("File access with protected paths"),
                        qsTr("Per-tool permission editing from the interface"),
                        qsTr("A persistent audit log on disk"),
                        qsTr("Scheduled and automated actions")
                    ]

                    delegate: Row {
                        required property string modelData
                        spacing: Theme.spacingSm

                        Text {
                            text: "TODO"
                            color: Theme.warning
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontNano
                            font.letterSpacing: Theme.trackingLabel
                        }

                        Text {
                            text: parent.modelData
                            color: Theme.textDim
                            font.family: Theme.bodyFamily
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }
        }
    }
}
