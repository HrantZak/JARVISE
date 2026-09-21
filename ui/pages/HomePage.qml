import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// The main screen.
///
/// Composition rather than layout: the Core sits at the optical centre with a
/// horizon rule running behind it, wings of information at the edges, and the
/// Command Center along the bottom. Nothing is arranged in a grid, because a
/// grid is what makes an interface read as a dashboard.
Item {
    id: home

    // The wings need the Core plus two panels plus breathing room. Below this
    // the page drops them rather than crushing the composition.
    readonly property bool wide: width > 940

    // --- Depth ------------------------------------------------------------

    ParticleField {
        anchors.fill: parent
        visible: false
        active: false
        tint: coreVisual.tint
        density: 30
    }

    // Horizon: a tick rule passing behind the Core, the line the whole screen
    // is balanced on.
    TickScale {
        anchors.verticalCenter: coreVisual.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        tint: coreVisual.tint
        visible: false
        baseOpacity: 0.09
        tickSpacing: 14
        majorEvery: 4
    }

    // The thin horizon is the main visual response to a conversation. It uses
    // the same real envelope as the Core, so the line is flat when there is no
    // sound and moves only while JARVIS is listening or speaking.
    VoiceWave {
        id: conversationWave
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: coreVisual.verticalCenter
        height: Math.min(160, home.height * 0.24)
        z: 2
        tint: coreVisual.tint
        active: coreVisual.reactsToAudio
        level: coreVisual.effectiveLevel
    }

    // --- The Core ---------------------------------------------------------

    AiCore {
        id: coreVisual
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter

        width: Math.min(320, Math.min(home.width * 0.36, home.height * 0.52))
        height: width
        opacity: reactsToAudio ? 0.72 : 0.48

        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

        coreState: Core.stateKey
        stateLabel: Core.stateLabel
        // The untranslated key is compared, never the label: the singleton is
        // registered by instance so its enum has no QML name to import, and a
        // localised label would break every branch below it.
        level: Core.stateKey === "LISTENING" ? Core.inputLevel : Core.outputLevel
        progress: Core.progress
        previewing: Core.previewing
        statusText: Core.statusText
    }

    // --- Left wing: subsystems -------------------------------------------

    GlassPanel {
        id: subsystems
        visible: false
        anchors.left: parent.left
        anchors.verticalCenter: coreVisual.verticalCenter
        width: Math.min(258, home.width * 0.26)
        height: subsystemColumn.implicitHeight + Theme.spacingLg * 2
        title: qsTr("SUBSYSTEMS")

        Column {
            id: subsystemColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingSm

            Repeater {
                model: [
                    { name: qsTr("Configuration"),  ready: true,  note: "" },
                    { name: qsTr("Logging"),        ready: true,  note: "" },
                    { name: qsTr("Telemetry"),      ready: true,  note: "" },
                    { name: qsTr("Interface"),      ready: true,  note: "" },
                    { name: qsTr("LLM engine"),     ready: Llm.loaded,
                      note: (App.language === "ru" ? "НУЖЕН API-КЛЮЧ" : "API KEY REQUIRED") },
                    { name: qsTr("Speech to text"), ready: Voice.sttReady,
                      note: qsTr("NO MODEL") },
                    { name: qsTr("Text to speech"), ready: Voice.ttsReady,
                      note: qsTr("NO VOICE") },
                    // Bound to the real subsystems rather than hardcoded. These
                    // three said PHASE 7, 9 and 10 long after they shipped in
                    // Phase 5 and 6 - the panel told the user a working
                    // subsystem did not exist yet, which is worse than saying
                    // nothing.
                    { name: qsTr("Tool engine"),    ready: Tools.enabled,
                      note: qsTr("DISABLED") },
                    { name: qsTr("Permissions"),    ready: Tools.enabled,
                      note: qsTr("DISABLED") },
                    { name: qsTr("Agent"),          ready: Agent.enabled,
                      note: qsTr("DISABLED") },
                    { name: qsTr("Memory"),         ready: Agent.memoryEnabled,
                      note: qsTr("OFF BY DEFAULT") }
                ]

                delegate: Item {
                    id: subsystemRow
                    required property var modelData

                    width: subsystemColumn.width
                    height: 19

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingSm

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 4
                            height: 4
                            radius: 2
                            color: subsystemRow.modelData.ready ? Theme.success : Theme.textDim
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: subsystemRow.modelData.name
                            color: subsystemRow.modelData.ready ? Theme.textSecondary
                                                                : Theme.textMuted
                            font.family: Theme.bodyFamily
                            font.pixelSize: Theme.fontSmall
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: subsystemRow.modelData.ready ? qsTr("READY")
                                                           : subsystemRow.modelData.note
                        color: subsystemRow.modelData.ready ? Theme.alpha(Theme.success, 0.85)
                                                            : Theme.textDim
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontNano
                        font.letterSpacing: Theme.trackingLabel
                    }
                }
            }
        }
    }

    // --- Right wing: machine ---------------------------------------------

    GlassPanel {
        id: machine
        visible: false
        anchors.right: parent.right
        anchors.verticalCenter: coreVisual.verticalCenter
        width: Math.min(258, home.width * 0.26)
        height: machineColumn.implicitHeight + Theme.spacingLg * 2
        title: qsTr("MACHINE")
        trailingLabel: Sys.gpuAvailable ? qsTr("LIVE") : qsTr("PARTIAL")
        trailingColor: Sys.gpuAvailable ? Theme.success : Theme.warning

        Column {
            id: machineColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd

            MetricReadout {
                width: parent.width
                label: qsTr("PROCESSOR")
                available: Sys.cpuAvailable
                percent: Sys.cpuPercent
                valueText: Math.round(Sys.cpuPercent) + "%"
                detailText: Sys.cpuTopology
                history: Sys.cpuHistory
                showGraph: true
            }

            MetricReadout {
                width: parent.width
                label: qsTr("GRAPHICS")
                available: Sys.gpuAvailable
                percent: Sys.gpuPercent
                valueText: Math.round(Sys.gpuPercent) + "%"
                detailText: Sys.gpuTemperatureAvailable ? Sys.gpuTemperature + "°C" : ""
                unavailableText: qsTr("NO NVML GPU")
                history: Sys.gpuHistory
                showGraph: true
            }

            MetricReadout {
                width: parent.width
                label: qsTr("MEMORY")
                available: Sys.memoryAvailable
                percent: Sys.memoryPercent
                valueText: Math.round(Sys.memoryPercent) + "%"
                detailText: Sys.memoryText
            }
        }
    }

    // --- Startup warnings -------------------------------------------------

    GlassPanel {
        id: warnings
        visible: false
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width * 0.6, 640)
        height: warningColumn.implicitHeight + Theme.spacingLg * 2
        title: qsTr("STARTUP WARNINGS")
        accentColor: Theme.warning
        highlighted: true

        Column {
            id: warningColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingXs

            Repeater {
                model: App.startupWarnings

                delegate: Text {
                    id: warningText
                    required property string modelData
                    width: warningColumn.width
                    text: "▲  " + modelData
                    color: Theme.warning
                    wrapMode: Text.WordWrap
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
    }

    // --- Command Center ---------------------------------------------------
    //
    // Shows what the system is doing in plain words. It never shows model
    // reasoning: the requirement is a safe status line, not a thought stream.

    GlassPanel {
        id: commandCenter
        visible: false
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 96
        title: qsTr("COMMAND CENTER")
        trailingLabel: Core.stateLabel
        trailingColor: Theme.coreColor(Core.stateKey)

        // State marker.
        Rectangle {
            id: stateMarker
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 42
            color: Theme.coreColor(Core.stateKey)

            Behavior on color { ColorAnimation { duration: Theme.durationSlow } }
        }

        Column {
            anchors.left: stateMarker.right
            anchors.leftMargin: Theme.spacingLg
            anchors.right: taskBlock.left
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXs

            Text {
                width: parent.width
                text: Core.statusText
                color: Theme.textPrimary
                elide: Text.ElideRight
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontSubtitle
                font.letterSpacing: Theme.trackingLabel
            }

            Text {
                width: parent.width
                text: Core.detailText
                color: Theme.textDim
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.family: Theme.bodyFamily
                font.pixelSize: Theme.fontSmall
            }
        }

        Column {
            id: taskBlock
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            width: 210
            spacing: Theme.spacingXs

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignRight
                text: qsTr("ACTIVE TASK")
                color: Theme.textDim
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignRight
                // The real task, when there is one. This said NONE and "no tool
                // engine until phase 7" for two phases after the agent shipped.
                text: Agent.busy && Agent.taskId !== "" ? Agent.taskId : qsTr("NONE")
                color: Agent.busy ? Theme.textMuted : Theme.textDim
                elide: Text.ElideLeft
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontBody
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignRight
                text: {
                    if (!Agent.enabled)
                        return qsTr("the agent is switched off");
                    if (!Agent.busy)
                        return qsTr("waiting for a request");
                    if (Agent.totalSteps > 0)
                        return qsTr("step %1 of %2%3")
                            .arg(Agent.currentStep)
                            .arg(Agent.totalSteps)
                            .arg(Agent.currentTool !== ""
                                 ? " · " + Agent.currentTool : "");
                    return Agent.agentStateLabel;
                }
                color: Theme.textDim
                elide: Text.ElideRight
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
            }
        }
    }
}
