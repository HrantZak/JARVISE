import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// Speech in, speech out.
///
/// Every value on this page is read from a running backend. The level meters
/// are RMS measurements of real samples, the device lists come from the
/// platform, and the state is the pipeline's own. Nothing here animates to look
/// busy: with no audio flowing the meters sit at zero.
Item {
    id: page

    /// Human-readable label for the pipeline state. The key stays untranslated
    /// and drives the colour; the label is what the user reads.
    function stateLabel(key) {
        switch (key) {
        case "DISABLED":     return qsTr("DISABLED")
        case "UNAVAILABLE":  return qsTr("UNAVAILABLE")
        case "IDLE":         return qsTr("READY")
        case "LISTENING":    return qsTr("LISTENING")
        case "TRANSCRIBING": return qsTr("TRANSCRIBING")
        case "THINKING":     return qsTr("THINKING")
        case "SYNTHESIZING": return qsTr("SYNTHESIZING")
        case "SPEAKING":     return qsTr("SPEAKING")
        case "ERROR":        return qsTr("VOICE ERROR")
        }
        return key
    }

    function stateLevel(key) {
        switch (key) {
        case "IDLE":         return StatusChip.Level.Ready
        case "LISTENING":
        case "TRANSCRIBING":
        case "THINKING":
        case "SYNTHESIZING":
        case "SPEAKING":     return StatusChip.Level.Pending
        case "ERROR":        return StatusChip.Level.Danger
        case "UNAVAILABLE":  return StatusChip.Level.Warning
        }
        return StatusChip.Level.Inactive
    }

    PageScaffold {
        anchors.fill: parent
        title: qsTr("VOICE")
        subtitle: App.language === "ru" ? "Скажите «Жарвис», дождитесь «Слушаю, сэр» и произнесите команду в течение 15 секунд. Можно сразу: «Жарвис, открой калькулятор»." : "Say Jarvis, wait for the acknowledgement, then give a command within 15 seconds. Or say: Jarvis, open calculator."
        statusText: Voice.loading ? (App.language === "ru" ? "ЗАГРУЗКА" : "LOADING") : page.stateLabel(Voice.stateKey)
        statusLevel: page.stateLevel(Voice.stateKey)
        GlassPanel {
            width: parent.width
            height: performance.implicitHeight + Theme.spacingLg * 2
            title: App.language === "ru" ? "ПРОИЗВОДИТЕЛЬНОСТЬ" : "PERFORMANCE"
            Column {
                id: performance
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd
                ToggleSwitch {
                    width: parent.width
                    label: App.language === "ru" ? "Распознавание на видеокарте" : "GPU speech recognition"
                    description: App.language === "ru" ? "Изменение применяется после перезапуска. DeepSeek не занимает видеопамять локальной языковой моделью." : "Applies after restart. DeepSeek does not use VRAM for a local language model."
                    checked: App.speechGpu
                    onToggled: function(value) { App.setSpeechGpu(value) }
                }
                OptionSelector {
                    width: parent.width
                    label: App.language === "ru" ? "Потоки процессора для голоса" : "CPU workers for speech"
                    description: App.language === "ru" ? "4 потока по умолчанию, чтобы оставить ресурсы рабочему столу. Изменение применяется после перезапуска." : "Default: 4 workers to leave resources for the desktop. Applies after restart."
                    options: ["2", "4", "8"]
                    current: String(App.speechThreads)
                    onSelected: function(value) { App.setSpeechThreads(Number(value)) }
                }
            }
        }

        // --- Pipeline ------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: pipelineColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("PIPELINE")
            trailingLabel: Voice.enabled ? qsTr("ON") : qsTr("OFF")
            trailingColor: Voice.enabled ? Theme.success : Theme.textDim
            accentColor: Voice.stateKey === "ERROR" ? Theme.danger : Theme.accent
            highlighted: Voice.enabled

            Column {
                id: pipelineColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingLg

                ToggleSwitch {
                    width: parent.width
                    label: qsTr("Voice enabled")
                    description: Voice.available
                                 ? qsTr("Opens the microphone and starts the pipeline.")
                                 : Voice.unavailableReason
                    checked: Voice.enabled

                    // Always operable. It used to be gated on Voice.available,
                    // which is false until the recognition model is loaded -
                    // and the model only loads when voice is switched on. The
                    // switch disabled the only thing that could enable it, so
                    // speech recognition could never be turned on at all.
                    //
                    // setEnabled() loads the model, starts the pipeline, and
                    // reports a real reason in unavailableReason if it cannot.
                    // Letting it try and say why beats refusing to try.
                    enabled: true
                    onToggled: function(value) { Voice.setEnabled(value) }
                }

                // The six stages, each showing what is actually behind it.
                Column {
                    width: parent.width
                    spacing: Theme.spacingSm

                    Repeater {
                        model: [
                            { name: qsTr("Microphone"), ready: Mic.available,
                              detail: Mic.available ? Mic.deviceName : qsTr("unavailable") },
                            { name: "STT", ready: Voice.sttReady,
                              detail: Voice.sttReady
                                      ? Voice.sttModel + "  ·  " +
                                        (Voice.sttOnGpu ? "GPU" : "CPU")
                                      : qsTr("model not loaded") },
                            { name: "LLM", ready: Llm.loaded,
                              detail: Llm.loaded ? Llm.loadedModelName
                                                 : qsTr("model not loaded") },
                            { name: "TTS", ready: Voice.ttsReady,
                              detail: Voice.ttsReady ? "Piper  ·  " + Voice.ttsVoice
                                                     : qsTr("no voice installed") },
                            { name: qsTr("Speaker"), ready: Speaker.available,
                              detail: Speaker.available ? Speaker.deviceName
                                                        : qsTr("unavailable") }
                        ]

                        delegate: Item {
                            id: stageRow
                            required property var modelData

                            width: pipelineColumn.width
                            height: 20

                            Row {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.spacingSm

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 4
                                    height: 4
                                    radius: 2
                                    color: stageRow.modelData.ready ? Theme.success
                                                                    : Theme.textDim
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 110
                                    text: stageRow.modelData.name
                                    color: stageRow.modelData.ready ? Theme.textSecondary
                                                                    : Theme.textMuted
                                    font.family: Theme.bodyFamily
                                    font.pixelSize: Theme.fontSmall
                                }
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 140
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: stageRow.modelData.detail
                                color: Theme.textDim
                                elide: Text.ElideRight
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontMicro
                            }
                        }
                    }
                }
            }
        }

        // --- Levels --------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: 132
            title: qsTr("LEVELS")
            trailingLabel: qsTr("REAL RMS")
            trailingColor: Theme.textDim

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingLg

                MetricReadout {
                    width: parent.width
                    label: qsTr("INPUT LEVEL")
                    available: Mic.running
                    // The meter is the microphone RMS scaled for display; a
                    // speaking voice sits well below 1.0 in absolute terms.
                    percent: Math.min(100, Core.inputLevel * 400)
                    valueText: Mic.running ? Core.inputLevel.toFixed(4) : "—"
                    detailText: Mic.running ? qsTr("capturing") : qsTr("microphone closed")
                    unavailableText: qsTr("NOT CAPTURING")
                    tint: Theme.accent
                }

                MetricReadout {
                    width: parent.width
                    label: qsTr("OUTPUT LEVEL")
                    available: Speaker.playing
                    percent: Math.min(100, Core.outputLevel * 400)
                    valueText: Speaker.playing ? Core.outputLevel.toFixed(4) : "—"
                    detailText: Speaker.playing ? qsTr("playing") : qsTr("silent")
                    unavailableText: qsTr("NOT PLAYING")
                    tint: Theme.accentBright
                }
            }
        }

        // --- Devices -------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: deviceColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("DEVICES")

            Column {
                id: deviceColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingLg

                OptionSelector {
                    width: parent.width
                    label: qsTr("Microphone")
                    description: qsTr("Selecting a device restarts capture immediately.")
                    uppercasePlainOptions: false
                    options: {
                        const list = []
                        for (let i = 0; i < Mic.devices.length; ++i) {
                            list.push({ code: Mic.devices[i].id,
                                        name: Mic.devices[i].name })
                        }
                        return list
                    }
                    current: Mic.deviceId
                    onSelected: function(id) { Mic.setDevice(id) }
                }

                OptionSelector {
                    width: parent.width
                    label: qsTr("Speaker")
                    uppercasePlainOptions: false
                    options: {
                        const list = []
                        for (let i = 0; i < Speaker.devices.length; ++i) {
                            list.push({ code: Speaker.devices[i].id,
                                        name: Speaker.devices[i].name })
                        }
                        return list
                    }
                    current: Speaker.deviceId
                    onSelected: function(id) { Speaker.setDevice(id) }
                }
            }
        }

        // --- Recognition and synthesis --------------------------------------

        GlassPanel {
            width: parent.width
            height: tuningColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("RECOGNITION AND SYNTHESIS")

            Column {
                id: tuningColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingLg

                OptionSelector {
                    width: parent.width
                    label: qsTr("Language")
                    description: qsTr("Recognition, the assistant's replies and the voice all follow the interface language.")
                    uppercasePlainOptions: false
                    options: App.languageOptions
                    current: App.language
                    onSelected: function(code) { App.setLanguage(code) }
                }

                OptionSelector {
                    width: parent.width
                    label: qsTr("Microphone sensitivity")
                    description: qsTr("How loud speech must be before recognition starts. Lower is more sensitive.")
                    options: ["0.008", "0.015", "0.030", "0.060"]
                    current: Voice.vadThreshold.toFixed(3)
                    onSelected: function(value) { Voice.vadThreshold = parseFloat(value) }
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("LAST HEARD")
                    value: Voice.lastTranscript.length > 0
                           ? Voice.lastTranscript
                           : qsTr("nothing recognised yet")
                    valueColor: Voice.lastTranscript.length > 0 ? Theme.textPrimary
                                                                : Theme.textDim
                }
            }
        }

        // --- Error ----------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: voiceErrorText.implicitHeight + Theme.spacingLg * 2
            visible: Voice.lastError.length > 0
            title: qsTr("ERROR")
            accentColor: Theme.danger
            highlighted: true

            Text {
                id: voiceErrorText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingLg
                text: Voice.lastError
                color: Theme.danger
                wrapMode: Text.WordWrap
                font.family: Theme.bodyFamily
                font.pixelSize: Theme.fontSmall
            }
        }

        // --- Privacy ---------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: privacyColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("PRIVACY")
            trailingLabel: qsTr("VERIFIABLE")
            trailingColor: Theme.success

            Column {
                id: privacyColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                InfoRow {
                    width: parent.width
                    label: qsTr("MICROPHONE")
                    value: Mic.running ? qsTr("open, audio kept in memory only")
                                       : qsTr("closed")
                }
                InfoRow {
                    width: parent.width
                    label: "STT"
                    value: qsTr("Whisper, local")
                }
                InfoRow {
                    width: parent.width
                    label: "TTS"
                    value: qsTr("Piper, local")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("NETWORK")
                    value: qsTr("no audio upload")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("RECORDINGS")
                    value: qsTr("nothing is written to disk")
                }
            }
        }
    }
}
