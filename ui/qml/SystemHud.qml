import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// The top status strip.
///
/// Not a table of numbers: a clock set in the display face, a tick rule, and a
/// run of instrument readouts separated by hairlines. Every value is a real
/// reading from SystemMonitor, and anything that cannot be read is shown as
/// unavailable rather than as zero.
Item {
    id: hud

    implicitHeight: Theme.hudHeight

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.backgroundRaised, 0.55)
    }

    TickScale {
        anchors.bottom: parent.bottom
        width: parent.width
        tint: Theme.accent
        baseOpacity: 0.22
        flipped: true
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingLg

        // --- Clock --------------------------------------------------------

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                text: Sys.clockText
                color: Theme.textPrimary
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontDisplay
                font.letterSpacing: 1.5
                font.weight: Font.Light
            }

            Text {
                text: Sys.dateText
                color: Theme.textDim
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingLabel
            }
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 34
            color: Theme.hairline
        }

        // --- Readouts -----------------------------------------------------

        HudMetric {
            anchors.verticalCenter: parent.verticalCenter
            label: "CPU"
            available: Sys.cpuAvailable
            percent: Sys.cpuPercent
            valueText: Math.round(Sys.cpuPercent).toString()
            suffix: "%"
        }

        HudMetric {
            anchors.verticalCenter: parent.verticalCenter
            label: "GPU"
            available: Sys.gpuAvailable
            percent: Sys.gpuPercent
            valueText: Math.round(Sys.gpuPercent).toString()
            suffix: Sys.gpuTemperatureAvailable ? "% · " + Sys.gpuTemperature + "°" : "%"
        }

        HudMetric {
            anchors.verticalCenter: parent.verticalCenter
            label: "RAM"
            available: Sys.memoryAvailable
            percent: Sys.memoryPercent
            valueText: Math.round(Sys.memoryPercent).toString()
            suffix: "%"
        }

        HudMetric {
            anchors.verticalCenter: parent.verticalCenter
            label: "VRAM"
            available: Sys.gpuAvailable
            percent: Sys.vramPercent
            valueText: Math.round(Sys.vramPercent).toString()
            suffix: "%"
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 34
            color: Theme.hairline
        }

        // --- Core state ---------------------------------------------------
        //
        // What the assistant as a whole is doing, resolved by AiCoreModel from
        // every subsystem's report. This is the arbitrated answer, not any one
        // subsystem's opinion.

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                text: qsTr("STATE")
                color: Theme.textDim
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.DemiBold
            }

            Text {
                text: Core.stateKey
                color: Theme.coreColor(Core.stateKey)
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingLabel
            }
        }

        Rectangle {
            width: Theme.borderWidth
            height: parent.height * 0.44
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.glassStroke
        }

        // --- Tools --------------------------------------------------------
        //
        // Only present while something is actually happening. The phase is the
        // coordinator's own, so what the strip shows is where the request has
        // genuinely reached — there is no timer here pretending to progress.

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            visible: Tools.busy

            Text {
                text: qsTr("TOOLS")
                color: Theme.textDim
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.DemiBold
            }

            Text {
                text: Tools.activity
                color: Tools.activity === "CONFIRMING" ? Theme.warning : Theme.accent
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingLabel
            }
        }

        Rectangle {
            width: Theme.borderWidth
            height: parent.height * 0.44
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.glassStroke
            visible: Tools.busy
        }

        // --- Voice --------------------------------------------------------
        //
        // Both meters are real RMS: the microphone's when capturing, the
        // speaker's when playing. With no audio flowing they read exactly zero,
        // which is why there is no animation behind them.

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Row {
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("VOICE")
                    color: Theme.textDim
                    font.family: Theme.displayFamily
                    font.pixelSize: Theme.fontNano
                    font.letterSpacing: Theme.trackingWide
                    font.weight: Font.DemiBold
                }

                Text {
                    text: Voice.enabled ? Voice.stateKey : qsTr("OFF")
                    color: Voice.stateKey === "ERROR"
                           ? Theme.danger
                           : (Voice.enabled ? Theme.accent : Theme.textDim)
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontNano
                    font.letterSpacing: Theme.trackingLabel
                }
            }

            // Input meter, driven by AudioCapture's RMS through the Core.
            Row {
                spacing: 4

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▼"
                    color: Mic.running ? Theme.textMuted : Theme.textDim
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontNano
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Repeater {
                        model: 14
                        delegate: Rectangle {
                            required property int index
                            readonly property bool lit:
                                Mic.running &&
                                Core.inputLevel * 400 >= (index + 1) / 14 * 100

                            width: 4
                            height: 3
                            color: lit ? Theme.accent
                                       : Theme.alpha(Theme.textDim,
                                                     Mic.running ? 0.26 : 0.12)

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast }
                            }
                        }
                    }
                }
            }

            // Output meter, driven by AudioPlayer's RMS.
            Row {
                spacing: 4

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▲"
                    color: Speaker.playing ? Theme.textMuted : Theme.textDim
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontNano
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Repeater {
                        model: 14
                        delegate: Rectangle {
                            required property int index
                            readonly property bool lit:
                                Speaker.playing &&
                                Core.outputLevel * 400 >= (index + 1) / 14 * 100

                            width: 4
                            height: 3
                            color: lit ? Theme.accentBright
                                       : Theme.alpha(Theme.textDim,
                                                     Speaker.playing ? 0.26 : 0.12)

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 34
            color: Theme.hairline
        }

        // --- Network ------------------------------------------------------

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                text: qsTr("NETWORK")
                color: Theme.textDim
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.DemiBold
            }

            Row {
                spacing: Theme.spacingSm

                Text {
                    text: "▼ " + (Sys.networkAvailable ? Sys.networkDownText : "––")
                    color: Sys.networkAvailable ? Theme.textSecondary : Theme.textDim
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMicro
                }

                Text {
                    text: "▲ " + (Sys.networkAvailable ? Sys.networkUpText : "––")
                    color: Sys.networkAvailable ? Theme.textSecondary : Theme.textDim
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMicro
                }
            }

            Sparkline {
                width: 128
                height: 12
                values: Sys.networkHistory
                capacity: Sys.historyCapacity
                lineColor: Theme.accent
                lineWidth: 1
                showBaseline: false
                filled: false
            }
        }
    }

    // --- AI model slot ----------------------------------------------------
    //
    // The HUD reserves the place the active model name will occupy. It says
    // NOT LOADED because no model engine exists yet, rather than showing a
    // plausible-looking model name.

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingSm

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                anchors.right: parent.right
                text: qsTr("AI MODEL")
                color: Theme.textDim
                font.family: Theme.displayFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingWide
                font.weight: Font.DemiBold
            }

            StatusChip {
                anchors.right: parent.right
                text: Llm.loaded
                      ? Llm.loadedModelName +
                        (Llm.gpuAccelerated ? qsTr(" · GPU") : qsTr(" · CPU"))
                      : (Llm.loading ? qsTr("LOADING…") : qsTr("NOT LOADED"))
                level: Llm.loaded
                       ? (Llm.gpuAccelerated ? StatusChip.Level.Ready
                                             : StatusChip.Level.Warning)
                       : StatusChip.Level.Inactive
            }
        }
    }
}
