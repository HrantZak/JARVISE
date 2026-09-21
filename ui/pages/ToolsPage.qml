import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// What JARVIS can do, and what it has done.
///
/// The permission next to each tool is read from the tool's own definition, not
/// from a list maintained here — so this page cannot drift from what the
/// security layer actually enforces. Nothing on it is illustrative.
Item {
    id: page

    function permissionColor(permission) {
        switch (permission) {
        case "READ_ONLY":        return Theme.success
        case "SAFE_ACTION":      return Theme.accent
        case "CONFIRM_REQUIRED": return Theme.warning
        case "DENIED":           return Theme.danger
        }
        return Theme.textDim
    }

    PageScaffold {
        anchors.fill: parent
        title: qsTr("TOOLS")
        subtitle: qsTr("The complete set of actions the assistant can take on this machine. Anything not listed here does not exist and cannot be requested.")
        statusText: Tools.enabled ? qsTr("ENABLED") : qsTr("DISABLED")
        statusLevel: Tools.enabled ? StatusChip.Level.Ready : StatusChip.Level.Inactive

        // --- Live activity -------------------------------------------------

        GlassPanel {
            width: parent.width
            height: activityColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("ACTIVITY")
            highlighted: Tools.busy
            accentColor: Tools.activity === "CONFIRMING" ? Theme.warning : Theme.accent
            visible: Tools.busy

            Column {
                id: activityColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                Text {
                    text: Tools.activityLabel
                    color: Theme.textPrimary
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontBody
                }

                Text {
                    text: Tools.activeTool
                    visible: Tools.activeTool !== ""
                    color: Theme.textMuted
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSmall
                }
            }
        }

        // --- The catalogue -------------------------------------------------

        GlassPanel {
            width: parent.width
            height: catalogueColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("AVAILABLE TOOLS")
            trailingLabel: Tools.tools.length

            Column {
                id: catalogueColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                Repeater {
                    model: Tools.tools

                    delegate: Item {
                        id: toolRow
                        required property var modelData

                        width: catalogueColumn.width
                        implicitHeight: toolText.implicitHeight

                        Column {
                            id: toolText
                            anchors.left: parent.left
                            anchors.right: permissionLabel.left
                            anchors.rightMargin: Theme.spacingLg
                            spacing: 2

                            Text {
                                text: toolRow.modelData.name
                                color: toolRow.modelData.available
                                       ? Theme.textPrimary : Theme.textDim
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontSmall
                            }

                            Text {
                                width: parent.width
                                text: toolRow.modelData.description
                                color: Theme.textMuted
                                font.family: Theme.bodyFamily
                                font.pixelSize: Theme.fontNano
                                wrapMode: Text.WordWrap
                            }
                        }

                        Text {
                            id: permissionLabel
                            anchors.right: parent.right
                            anchors.top: parent.top

                            text: toolRow.modelData.permissionLabel
                            color: page.permissionColor(toolRow.modelData.permission)
                            font.family: Theme.displayFamily
                            font.pixelSize: Theme.fontNano
                            font.letterSpacing: Theme.trackingWide
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        // --- What is enforced ----------------------------------------------

        GlassPanel {
            width: parent.width
            height: guaranteesColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("ENFORCED IN CODE")

            Column {
                id: guaranteesColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        qsTr("The assistant cannot run shell commands. No tool accepts a command line."),
                        qsTr("The assistant cannot name a file or a program by path. No tool accepts one."),
                        qsTr("The assistant cannot add tools, change permissions or confirm on your behalf."),
                        qsTr("Actions marked “asks first” never run until you allow that exact request."),
                        qsTr("Results from tools are treated as data. Instructions inside them are ignored.")
                    ]

                    delegate: Row {
                        required property string modelData
                        width: guaranteesColumn.width
                        spacing: Theme.spacingSm

                        Text {
                            text: "—"
                            color: Theme.success
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSmall
                        }

                        Text {
                            width: guaranteesColumn.width - Theme.spacingSm * 3
                            text: parent.modelData
                            color: Theme.textSecondary
                            font.family: Theme.bodyFamily
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        // --- Audit ----------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: auditColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("AUDIT")
            trailingLabel: Tools.auditEnabled ? Tools.audit.count : qsTr("OFF")

            Column {
                id: auditColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                Text {
                    width: parent.width
                    visible: Tools.audit.count === 0
                    text: Tools.auditEnabled
                          ? qsTr("Nothing has been requested yet.")
                          : qsTr("Auditing is switched off in the configuration.")
                    color: Theme.textDim
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    // Bound straight to the model, so the roles arrive by name
                    // and the delegate cannot drift out of step with the C++
                    // side the way a hand-copied array would.
                    model: Tools.audit

                    delegate: Row {
                        required property string timestamp
                        required property string eventLabel
                        required property string tool
                        required property string arguments
                        required property bool ok

                        width: auditColumn.width
                        spacing: Theme.spacingMd

                        Text {
                            text: parent.timestamp
                            color: Theme.textDim
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontNano
                        }

                        Text {
                            text: parent.eventLabel
                            color: parent.ok ? Theme.textSecondary : Theme.warning
                            font.family: Theme.bodyFamily
                            font.pixelSize: Theme.fontNano
                        }

                        Text {
                            text: parent.arguments === ""
                                  ? parent.tool
                                  : parent.tool + " (" + parent.arguments + ")"
                            color: Theme.textMuted
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontNano
                            elide: Text.ElideRight
                        }
                    }
                }

                HudButton {
                    text: qsTr("Clear the audit log")
                    visible: Tools.audit.count > 0
                    onClicked: Tools.clearAudit()
                }
            }
        }
    }

}
