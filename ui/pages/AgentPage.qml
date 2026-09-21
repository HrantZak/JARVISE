import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// What the agent is doing, step by step.
///
/// Every value here is read from the running `AgentLoop`. There is no state
/// machine in this file and no timer: the progress bar is finished steps over
/// total steps, and when nothing is running the page says so rather than
/// animating. Stop calls the one production stop; this page cannot set a task's
/// status, cancel a step or approve anything by itself.
Item {
    id: page

    /// Colour for a step's state. The keys come from `agent::stepStatusKey()`
    /// and are never translated - only what the user reads is.
    function stepColor(status) {
        switch (status) {
        case "SUCCEEDED":             return Theme.success
        case "RUNNING":               return Theme.accentBright
        case "AWAITING_CONFIRMATION": return Theme.warning
        case "FAILED":                return Theme.danger
        case "CANCELLED":
        case "SKIPPED":               return Theme.textDim
        }
        return Theme.textMuted
    }

    function stepLabel(status) {
        switch (status) {
        case "PENDING":               return qsTr("waiting")
        case "AWAITING_CONFIRMATION": return qsTr("needs your confirmation")
        case "RUNNING":               return qsTr("running")
        case "SUCCEEDED":             return qsTr("done")
        case "FAILED":                return qsTr("failed")
        case "SKIPPED":               return qsTr("skipped")
        case "CANCELLED":             return qsTr("stopped")
        }
        return status
    }

    function taskStatusLabel(status) {
        switch (status) {
        case "CREATED":   return qsTr("CREATED")
        case "RUNNING":   return qsTr("RUNNING")
        case "COMPLETED": return qsTr("COMPLETED")
        case "CANCELLED": return qsTr("STOPPED")
        case "FAILED":    return qsTr("FAILED")
        }
        return status
    }

    function taskStatusLevel(status) {
        switch (status) {
        case "RUNNING":   return StatusChip.Level.Pending
        case "COMPLETED": return StatusChip.Level.Ready
        case "FAILED":    return StatusChip.Level.Danger
        case "CANCELLED": return StatusChip.Level.Warning
        }
        return StatusChip.Level.Inactive
    }

    PageScaffold {
        anchors.fill: parent
        title: qsTr("AGENT")
        subtitle: qsTr("What the assistant is doing right now, and what it has been asked to do. Every step goes through the same checks as a single action.")
        statusText: Agent.busy ? page.taskStatusLabel(Agent.taskStatus)
                               : (Agent.enabled ? qsTr("READY") : qsTr("DISABLED"))
        statusLevel: Agent.busy ? page.taskStatusLevel(Agent.taskStatus)
                                : (Agent.enabled ? StatusChip.Level.Ready
                                                 : StatusChip.Level.Inactive)

        // --- The task in progress -------------------------------------------

        GlassPanel {
            width: parent.width
            height: taskColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("CURRENT TASK")
            trailingLabel: Agent.busy ? Agent.taskId : ""
            highlighted: Agent.busy
            accentColor: Agent.agentState === "AWAITING_CONFIRMATION"
                         ? Theme.warning : Theme.accent

            Column {
                id: taskColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                // Nothing running. Said plainly rather than shown as an empty
                // progress bar, which reads as "stuck" rather than "idle".
                Text {
                    width: parent.width
                    visible: !Agent.busy
                    text: Agent.enabled
                          ? qsTr("No task is running. Ask a question to start one.")
                          : qsTr("The agent is switched off in the configuration.")
                    color: Theme.textDim
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    visible: Agent.busy
                    text: Agent.userRequest
                    color: Theme.textPrimary
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }

                Row {
                    visible: Agent.busy
                    spacing: Theme.spacingMd

                    Text {
                        text: Agent.agentStateLabel
                        color: Theme.coreColor(Core.stateKey)
                        font.family: Theme.bodyFamily
                        font.pixelSize: Theme.fontSmall
                    }

                    Text {
                        visible: Agent.currentTool !== ""
                        text: Agent.currentTool
                        color: Theme.textMuted
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSmall
                    }

                    Text {
                        // Only while something is actually being retried. A
                        // permanent "0 retries" tells nobody anything.
                        visible: Agent.retryCount > 0
                        text: qsTr("retry %1").arg(Agent.retryCount)
                        color: Theme.warning
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSmall
                    }
                }

                // --- Progress -----------------------------------------------
                //
                // Finished steps over total steps. A real fraction of real
                // work: there is no timer behind this, so a task that is stuck
                // looks stuck.
                Column {
                    width: parent.width
                    visible: Agent.busy && Agent.totalSteps > 0
                    spacing: Theme.spacingSm

                    Row {
                        width: parent.width
                        spacing: Theme.spacingMd

                        Text {
                            text: qsTr("Step %1 of %2")
                                    .arg(Agent.currentStep).arg(Agent.totalSteps)
                            color: Theme.textSecondary
                            font.family: Theme.bodyFamily
                            font.pixelSize: Theme.fontSmall
                        }

                        Text {
                            text: Math.round(Agent.progress * 100) + "%"
                            color: Theme.textMuted
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSmall
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 3
                        radius: 1
                        color: Theme.alpha(Theme.accent, 0.14)

                        Rectangle {
                            width: parent.width * Agent.progress
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accentBright

                            Behavior on width {
                                NumberAnimation {
                                    duration: Theme.durationNormal
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }
                    }
                }

                // --- Failure ------------------------------------------------

                Text {
                    width: parent.width
                    visible: Agent.lastError !== ""
                    text: Agent.lastError
                    color: Theme.danger
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                HudButton {
                    text: qsTr("Stop")
                    visible: Agent.busy
                    // The one production stop. This page cannot cancel a task
                    // by any other route, and it never writes a status itself:
                    // what it shows next is whatever the backend reports.
                    onClicked: Agent.stop()
                }
            }
        }

        // --- The plan --------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: stepsColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("STEPS")
            trailingLabel: Agent.totalSteps > 0 ? Agent.totalSteps : ""
            visible: Agent.totalSteps > 0

            Column {
                id: stepsColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                Repeater {
                    model: Agent.steps

                    delegate: Item {
                        id: stepRow
                        required property var modelData
                        required property int index

                        width: stepsColumn.width
                        implicitHeight: stepText.implicitHeight

                        Text {
                            id: stepNumber
                            anchors.left: parent.left
                            anchors.top: parent.top
                            width: 24
                            text: (stepRow.index + 1) + "."
                            color: Theme.textDim
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSmall
                        }

                        Column {
                            id: stepText
                            anchors.left: stepNumber.right
                            anchors.right: stepState.left
                            anchors.rightMargin: Theme.spacingMd
                            spacing: 2

                            Text {
                                text: stepRow.modelData.arguments === ""
                                      ? stepRow.modelData.tool
                                      : stepRow.modelData.tool + " ("
                                        + stepRow.modelData.arguments + ")"
                                color: Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                visible: stepRow.modelData.error !== ""
                                text: stepRow.modelData.error
                                color: Theme.danger
                                font.family: Theme.bodyFamily
                                font.pixelSize: Theme.fontNano
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }

                        Row {
                            id: stepState
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: Theme.spacingSm

                            Text {
                                // Attempts beyond the first, shown only when
                                // there were any.
                                visible: stepRow.modelData.attempts > 1
                                text: qsTr("×%1").arg(stepRow.modelData.attempts)
                                color: Theme.warning
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontNano
                            }

                            Text {
                                text: page.stepLabel(stepRow.modelData.status)
                                color: page.stepColor(stepRow.modelData.status)
                                font.family: Theme.displayFamily
                                font.pixelSize: Theme.fontNano
                                font.letterSpacing: Theme.trackingWide
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
        }

        // --- Memory ----------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: memoryColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("MEMORY")
            trailingLabel: Agent.memoryEnabled ? qsTr("ON") : qsTr("OFF")
            trailingColor: Agent.memoryEnabled ? Theme.success : Theme.textDim

            Column {
                id: memoryColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                Text {
                    width: parent.width
                    text: Agent.memoryEnabled
                          ? qsTr("The assistant may keep a few facts between tasks. They are shown to the model as data and carry no permissions.")
                          : qsTr("The assistant remembers nothing between tasks.")
                    color: Theme.textSecondary
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("KEPT")
                    value: Agent.memoryEntryCount
                    monospace: true
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("AFTER RESTART")
                    value: Agent.memoryPersistent
                           ? qsTr("Kept on disk.")
                           : qsTr("Forgotten. Nothing is written to disk.")
                    monospace: false
                }
            }
        }
    }
}
