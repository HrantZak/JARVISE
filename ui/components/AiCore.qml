import QtQuick
import QtQuick.Shapes
import QtQuick.Window
import Jarvis.Theme

/// The AI Core: the one element the whole interface is built around.
///
/// It is a state machine with eight visual behaviours, driven entirely by
/// properties. Phase 3 onwards changes `coreState`, `level` and `progress` from
/// C++ and the Core reacts; no QML here needs to be revisited to connect a real
/// engine.
///
/// Every animation is bound to the state that needs it, so exactly one set of
/// animators runs at a time and OFFLINE runs none at all.
Item {
    id: core

    // --- Inputs ----------------------------------------------------------

    /// Untranslated state key: one of OFFLINE, IDLE, LISTENING, THINKING,
    /// EXECUTING, SPEAKING, WARNING, ERROR. All the logic below branches on
    /// this, so it must never be localised. Named coreState because Item
    /// already defines `state`.
    property string coreState: "OFFLINE"

    /// The same state, translated for display. Falls back to the key.
    property string stateLabel: coreState

    /// Audio envelope, 0..1. Microphone while listening, speech while speaking.
    property real level: 0.0

    /// Task progress, 0..1, used by EXECUTING.
    property real progress: 0.0

    /// True when a developer is previewing a state with no engine attached.
    property bool previewing: false

    /// Headline shown under the state name.
    property string statusText: ""

    implicitWidth: 340
    implicitHeight: 340

    // --- Derived ---------------------------------------------------------

    // Not readonly: a Behavior cannot be attached to a read-only property, and
    // the colour transition between states is part of the language.
    property color tint: Theme.coreColor(coreState)
    readonly property real outerRadius: Math.min(width, height) / 2 - 6
    readonly property bool animate: visible && Window.window !== null && Window.window.visibility !== Window.Minimized && Window.window.visibility !== Window.Hidden
    readonly property bool offline: coreState === "OFFLINE"
    readonly property bool idle: coreState === "IDLE"
    readonly property bool listening: coreState === "LISTENING"
    // Planning and evaluating are deliberation, and they wear the deliberation
    // visuals. The state text below the Core says which one it is; inventing a
    // separate animation for each would be decoration, not information.
    readonly property bool thinking: coreState === "THINKING" || coreState === "PLANNING"
    readonly property bool executing: coreState === "EXECUTING"
                                      || coreState === "RECOVERING"
    readonly property bool speaking: coreState === "SPEAKING"
    // Waiting for a person is an attention state, not a busy one: it shares the
    // warning treatment so a pending question is visible across the room.
    readonly property bool warning: coreState === "WARNING" || coreState === "CONFIRMING"
    readonly property bool errored: coreState === "ERROR"

    readonly property bool reactsToAudio: listening || speaking

    // Preview drives a synthetic envelope so the audio-reactive states can be
    // inspected before a microphone exists. It is only ever non-zero while
    // `previewing` is true, and a PREVIEW badge is shown throughout.
    property real previewLevel: 0.0
    readonly property real effectiveLevel: previewing ? previewLevel : level

    SequentialAnimation on previewLevel {
        running: core.animate && (core.previewing && core.reactsToAudio)
        loops: Animation.Infinite
        NumberAnimation { to: 0.85; duration: 380; easing.type: Theme.easeStandard }
        NumberAnimation { to: 0.20; duration: 300; easing.type: Theme.easeStandard }
        NumberAnimation { to: 0.62; duration: 260; easing.type: Theme.easeStandard }
        NumberAnimation { to: 0.08; duration: 440; easing.type: Theme.easeStandard }
    }

    Behavior on tint {
        ColorAnimation { duration: Theme.durationSlow; easing.type: Theme.easeStandard }
    }

    // =====================================================================
    // Layer 1 - ambient glow
    // =====================================================================

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        opacity: core.offline ? 0.20 : (0.42 + core.effectiveLevel * 0.35)

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationNormal }
        }

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: core.width / 2
                centerY: core.height / 2
                centerRadius: core.outerRadius * 1.15
                focalX: core.width / 2
                focalY: core.height / 2
                GradientStop { position: 0.0; color: Theme.alpha(core.tint, 0.34) }
                GradientStop { position: 0.45; color: Theme.alpha(core.tint, 0.11) }
                GradientStop { position: 1.0; color: "transparent" }
            }

            PathAngleArc {
                centerX: core.width / 2
                centerY: core.height / 2
                radiusX: core.outerRadius * 1.15
                radiusY: core.outerRadius * 1.15
                startAngle: 0
                sweepAngle: 360
            }
        }
    }

    // =====================================================================
    // Layer 2 - tick ring
    //
    // Rotates at a speed that tells you what the machine is doing: still when
    // offline, a slow drift when idle, brisk while working.
    // =====================================================================

    TickRing {
        id: ticks
        anchors.fill: parent
        tint: core.tint
        tickCount: 96
        majorEvery: 8
        radiusRatio: 0.5
        tickOpacity: core.offline ? 0.18 : 0.55
        tickLength: core.speaking ? 5 + core.effectiveLevel * 16 : 7
        majorTickLength: core.speaking ? 11 + core.effectiveLevel * 18 : 13

        RotationAnimator on rotation {
            running: core.animate && (!core.offline)
            from: 0
            to: 360
            duration: core.executing ? 9000 : (core.thinking ? 14000 : 52000)
            loops: Animation.Infinite
        }
    }

    // =====================================================================
    // Layer 3 - structural arcs
    // =====================================================================

    // OFFLINE: a deliberately broken ring. Four short arcs with wide gaps read
    // as "not connected" at a glance, before you have read a single word.
    Item {
        anchors.fill: parent
        visible: core.offline

        Repeater {
            model: [-84, 6, 96, 186]
            delegate: CoreArc {
                required property var modelData
                anchors.fill: parent
                arcRadius: core.outerRadius * 0.80
                startAngle: modelData
                sweepAngle: 62
                strokeColor: core.tint
                strokeWidth: 2
                strokeOpacity: 0.75
            }
        }
    }

    // Connected states: a continuous, quiet outer ring.
    CoreArc {
        anchors.fill: parent
        visible: !core.offline
        arcRadius: core.outerRadius * 0.80
        startAngle: 0
        sweepAngle: 360
        strokeColor: core.tint
        strokeWidth: 1
        strokeOpacity: 0.28
    }

    // THINKING: two arc groups counter-rotating at different rates. The motion
    // reads as deliberation without ever exposing what the model is thinking.
    Item {
        id: thinkingOuter
        anchors.fill: parent
        visible: core.thinking

        Repeater {
            model: [0, 120, 240]
            delegate: CoreArc {
                required property var modelData
                anchors.fill: parent
                arcRadius: core.outerRadius * 0.80
                startAngle: modelData
                sweepAngle: 46
                strokeColor: core.tint
                strokeWidth: 2
                strokeOpacity: 0.9
            }
        }

        RotationAnimator on rotation {
            running: core.animate && (thinkingOuter.visible)
            from: 0; to: 360; duration: 3200; loops: Animation.Infinite
        }
    }

    Item {
        id: thinkingInner
        anchors.fill: parent
        visible: core.thinking

        Repeater {
            model: [40, 200]
            delegate: CoreArc {
                required property var modelData
                anchors.fill: parent
                arcRadius: core.outerRadius * 0.62
                startAngle: modelData
                sweepAngle: 74
                strokeColor: Theme.accentBright
                strokeWidth: 1
                strokeOpacity: 0.7
            }
        }

        RotationAnimator on rotation {
            running: core.animate && (thinkingInner.visible)
            from: 360; to: 0; duration: 5200; loops: Animation.Infinite
        }
    }

    // EXECUTING: a real progress arc bound to `progress`, so once the tool
    // engine reports completion this becomes a genuine progress indicator
    // rather than a spinner.
    CoreArc {
        id: progressArc
        anchors.fill: parent
        visible: core.executing
        arcRadius: core.outerRadius * 0.88
        startAngle: -90
        sweepAngle: Math.max(6, core.progress * 360)
        strokeColor: Theme.accentBright
        strokeWidth: 3
        strokeOpacity: 0.95
        capStyle: ShapePath.RoundCap

        Behavior on sweepAngle {
            NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easeStandard }
        }
    }

    // EXECUTING: a sweeping head that keeps the ring feeling driven while the
    // reported progress is still coarse.
    Item {
        id: executingSweep
        anchors.fill: parent
        visible: core.executing

        CoreArc {
            anchors.fill: parent
            arcRadius: core.outerRadius * 0.72
            startAngle: 0
            sweepAngle: 34
            strokeColor: core.tint
            strokeWidth: 2
            strokeOpacity: 0.8
            capStyle: ShapePath.RoundCap
        }

        RotationAnimator on rotation {
            running: core.animate && (executingSweep.visible)
            from: 0; to: 360; duration: 1400; loops: Animation.Infinite
        }
    }

    // LISTENING and SPEAKING: a ring whose radius follows the audio envelope.
    CoreArc {
        anchors.fill: parent
        visible: core.reactsToAudio
        arcRadius: core.outerRadius * (0.52 + core.effectiveLevel * 0.30)
        startAngle: 0
        sweepAngle: 360
        strokeColor: core.tint
        strokeWidth: 2
        strokeOpacity: 0.55 + core.effectiveLevel * 0.4

        Behavior on arcRadius {
            NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
        }
    }

    // LISTENING: pulses travelling outward - the visual equivalent of
    // "I am hearing you".
    Repeater {
        model: 2
        delegate: CoreArc {
            id: pulse
            required property int index

            anchors.fill: parent
            visible: core.listening
            startAngle: 0
            sweepAngle: 360
            strokeColor: core.tint
            strokeWidth: 1
            arcRadius: core.outerRadius * 0.50
            strokeOpacity: 1
            opacity: 0

            SequentialAnimation {
                running: core.animate && (core.listening)
                loops: Animation.Infinite
                PauseAnimation { duration: pulse.index * 900 }
                ParallelAnimation {
                    NumberAnimation {
                        target: pulse; property: "scale"
                        from: 1.0; to: 1.96
                        duration: 1800; easing.type: Easing.OutCubic
                    }
                    SequentialAnimation {
                        NumberAnimation {
                            target: pulse; property: "opacity"
                            from: 0; to: 0.5; duration: 260
                        }
                        NumberAnimation {
                            target: pulse; property: "opacity"
                            to: 0; duration: 1540; easing.type: Easing.InQuad
                        }
                    }
                }
            }
        }
    }

    // =====================================================================
    // Layer 4 - inner disc
    // =====================================================================

    // A lightweight pseudo-3D particle sphere. The dots are projected from
    // latitude/longitude coordinates, so the front half naturally brightens
    // while the back half fades. It gives the Core a physical centre without
    // introducing a scene graph or a per-frame canvas rebuild.
    Item {
        id: sphere
        anchors.centerIn: parent
        width: core.outerRadius * 1.18
        height: width
        visible: true
        opacity: core.offline ? 0.28 : (core.thinking || core.executing ? 0.98 : 0.82)

        property real sphereRotation: 0
        property real spherePulse: 0
        property real meridianRotation: 0
        readonly property int particleCount: 84
        readonly property real sphereRadius: width * 0.33

        NumberAnimation on sphereRotation {
            running: core.animate && sphere.visible
            from: 0; to: 360
            duration: core.offline ? 42000 : (core.executing ? 5200 : (core.thinking ? 9800 : (core.listening || core.speaking ? 11500 : 22000)))
            loops: Animation.Infinite
        }

        // Two moving meridians create the depth cue of a real globe. They
        // rotate at different speeds, so the centre never looks like a flat
        // static badge even when the assistant is idle.
        NumberAnimation on meridianRotation {
            running: core.animate && sphere.visible
            from: 0
            to: 360
            duration: core.listening || core.speaking ? 5200 : 14500
            loops: Animation.Infinite
        }

        SequentialAnimation on spherePulse {
            running: core.animate && sphere.visible && !core.offline
            loops: Animation.Infinite
            NumberAnimation { to: 1; duration: 900; easing.type: Theme.easeBreath }
            NumberAnimation { to: 0; duration: 900; easing.type: Theme.easeBreath }
        }

        Repeater {
            model: sphere.particleCount
            delegate: Rectangle {
                required property int index
                readonly property real latitude: -1 + 2 * (index + 0.5) / sphere.particleCount
                readonly property real bandRadius: Math.sqrt(Math.max(0, 1 - latitude * latitude))
                readonly property real angle: index * 2.399963 + sphere.sphereRotation * Math.PI / 180
                readonly property real depth: Math.cos(angle) * bandRadius
                readonly property real projectedX: sphere.width / 2 + Math.sin(angle) * bandRadius * sphere.sphereRadius
                readonly property real projectedY: sphere.height / 2 + latitude * sphere.sphereRadius
                readonly property real front: Math.max(0, (depth + 1) / 2)
                readonly property real twinkle: 0.5 + 0.5 * Math.sin(index * 1.73 + sphere.sphereRotation * Math.PI / 90)
                width: 1.2 + front * 3.5 + sphere.spherePulse * (0.5 + twinkle)
                height: width
                x: projectedX - width / 2
                y: projectedY - height / 2
                radius: width / 2
                color: Theme.alpha(core.tint, core.offline ? 0.14 + front * 0.22 : 0.14 + front * (0.68 + twinkle * 0.22))
                scale: 0.64 + front * 0.44
            }
        }

        // A subtle equatorial trace makes the point cloud read as one object.
        CoreArc {
            anchors.fill: parent
            arcRadius: sphere.sphereRadius
            startAngle: 0
            sweepAngle: 360
            strokeColor: core.tint
            strokeWidth: 1
            strokeOpacity: 0.12 + sphere.spherePulse * 0.10
        }

        Repeater {
            model: [0, 60, 120]
            delegate: CoreArc {
                required property int index
                anchors.fill: parent
                arcRadius: sphere.sphereRadius * (0.82 + index * 0.05)
                startAngle: index * 17
                sweepAngle: 360
                strokeColor: core.tint
                strokeWidth: index === 1 ? 1.4 : 1
                strokeOpacity: (core.offline ? 0.08 : 0.18) + sphere.spherePulse * 0.08
                scale: 1
                transform: Scale { yScale: 0.38 + index * 0.18 }
            }
        }

        Item {
            id: meridianA
            anchors.fill: parent
            rotation: sphere.meridianRotation
            transformOrigin: Item.Center

            CoreArc {
                anchors.fill: parent
                arcRadius: sphere.sphereRadius * 0.98
                startAngle: 0
                sweepAngle: 360
                strokeColor: core.tint
                strokeWidth: 1
                strokeOpacity: core.offline ? 0.08 : 0.24 + sphere.spherePulse * 0.10
                transform: Scale {
                    origin.x: meridianA.width / 2
                    origin.y: meridianA.height / 2
                    yScale: 0.28
                }
            }
        }

        Item {
            id: meridianB
            anchors.fill: parent
            rotation: -sphere.meridianRotation * 0.63
            transformOrigin: Item.Center

            CoreArc {
                anchors.fill: parent
                arcRadius: sphere.sphereRadius * 0.86
                startAngle: 0
                sweepAngle: 360
                strokeColor: Theme.accentBright
                strokeWidth: 1
                strokeOpacity: core.offline ? 0.06 : 0.18 + sphere.spherePulse * 0.08
                transform: Scale {
                    origin.x: meridianB.width / 2
                    origin.y: meridianB.height / 2
                    yScale: 0.18
                }
            }
        }

        // Orbit arcs and four navigator points add a second animation layer
        // without a particle system or a per-frame canvas redraw.
        Item {
            id: orbitA
            anchors.fill: parent
            rotation: sphere.sphereRotation * 0.42
            transformOrigin: Item.Center

            CoreArc {
                anchors.fill: parent
                arcRadius: sphere.sphereRadius * 1.18
                startAngle: -32
                sweepAngle: 138
                strokeColor: core.tint
                strokeWidth: 1
                strokeOpacity: core.offline ? 0.08 : 0.30
                capStyle: ShapePath.RoundCap
                transform: Scale {
                    origin.x: orbitA.width / 2
                    origin.y: orbitA.height / 2
                    yScale: 0.34
                }
            }
        }

        Item {
            id: orbitB
            anchors.fill: parent
            rotation: -sphere.sphereRotation * 0.27
            transformOrigin: Item.Center

            CoreArc {
                anchors.fill: parent
                arcRadius: sphere.sphereRadius * 1.08
                startAngle: 148
                sweepAngle: 92
                strokeColor: Theme.accentBright
                strokeWidth: 1
                strokeOpacity: core.offline ? 0.06 : 0.22
                capStyle: ShapePath.RoundCap
                transform: Scale {
                    origin.x: orbitB.width / 2
                    origin.y: orbitB.height / 2
                    yScale: 0.48
                }
            }
        }

        Repeater {
            model: 4
            delegate: Rectangle {
                required property int index
                readonly property real orbitAngle: sphere.sphereRotation * Math.PI / 180 * 0.72
                                               + index * Math.PI / 2
                readonly property real orbitRadius: sphere.sphereRadius * 1.20
                readonly property real pointSize: 2.0 + sphere.spherePulse * 1.2
                width: pointSize
                height: width
                x: sphere.width / 2 + Math.cos(orbitAngle) * orbitRadius - width / 2
                y: sphere.height / 2 + Math.sin(orbitAngle) * orbitRadius * 0.34 - height / 2
                radius: width / 2
                color: Theme.alpha(core.tint, core.offline ? 0.16 : 0.70)
            }
        }
    }

    Item {
        id: disc
        anchors.centerIn: parent
        width: core.outerRadius * 1.06
        height: width

        // IDLE: a slow, even breath. Nothing else in the interface moves this
        // slowly, which is what makes the Core feel alive rather than busy.
        SequentialAnimation on scale {
            running: core.animate && (core.idle)
            loops: Animation.Infinite
            NumberAnimation { to: 1.035; duration: 2600; easing.type: Theme.easeBreath }
            NumberAnimation { to: 0.975; duration: 2600; easing.type: Theme.easeBreath }
        }

        // ERROR: an unsteady, low-amplitude tremor. Reads as a fault rather
        // than as decoration.
        SequentialAnimation on anchors.horizontalCenterOffset {
            running: core.animate && (core.errored)
            loops: Animation.Infinite
            NumberAnimation { to: 2; duration: 70 }
            NumberAnimation { to: -2; duration: 60 }
            NumberAnimation { to: 1; duration: 80 }
            NumberAnimation { to: 0; duration: 55 }
            PauseAnimation { duration: 620 }
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true

            ShapePath {
                strokeColor: Theme.alpha(core.tint, core.offline ? 0.20 : 0.34)
                strokeWidth: 1
                fillGradient: RadialGradient {
                    centerX: disc.width / 2
                    centerY: disc.height / 2
                    centerRadius: disc.width / 2
                    focalX: disc.width / 2
                    focalY: disc.height / 2
                    GradientStop { position: 0.0; color: Theme.alpha(core.tint, 0.16) }
                    GradientStop { position: 0.7; color: Theme.alpha(core.tint, 0.04) }
                    GradientStop { position: 1.0; color: "transparent" }
                }

                PathAngleArc {
                    centerX: disc.width / 2
                    centerY: disc.height / 2
                    radiusX: disc.width / 2 - 1
                    radiusY: disc.height / 2 - 1
                    startAngle: 0
                    sweepAngle: 360
                }
            }
        }

        // WARNING: the disc breathes brightness instead of size. Amber, slow,
        // impossible to miss and impossible to confuse with activity.
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.alpha(Theme.warning, 0.10)
            visible: core.warning

            SequentialAnimation on opacity {
                running: core.animate && (core.warning)
                loops: Animation.Infinite
                NumberAnimation { to: 1.0; duration: 900; easing.type: Theme.easeBreath }
                NumberAnimation { to: 0.15; duration: 900; easing.type: Theme.easeBreath }
            }
        }
    }

    // =====================================================================
    // Layer 5 - readout
    // =====================================================================

    Column {
        anchors.centerIn: parent
        width: core.width * 0.62
        spacing: Theme.spacingSm

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: core.stateLabel
            color: core.tint
            font.family: Theme.displayFamily
            // Russian state names are longer than the English ones (ОБРАБАТЫВАЮ
            // against THINKING), so the display size steps down rather than
            // letting the word collide with the ring.
            font.pixelSize: core.stateLabel.length > 9
                            ? Theme.fontTitle
                            : Theme.fontDisplay
            font.letterSpacing: core.stateLabel.length > 9
                                ? Theme.trackingWide
                                : Theme.trackingHero
            font.weight: Font.Light

            Behavior on color {
                ColorAnimation { duration: Theme.durationSlow }
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 54
            height: 1
            color: Theme.alpha(core.tint, 0.5)
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            visible: core.statusText.length > 0
            text: core.statusText
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.family: Theme.bodyFamily
            font.pixelSize: Theme.fontSmall
        }

        // The preview badge exists so nobody can mistake a previewed state for
        // a running engine.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: core.previewing
            width: previewLabel.implicitWidth + Theme.spacingMd
            height: 18
            radius: Theme.radiusSm
            color: Theme.alpha(Theme.warning, 0.12)
            border.width: 1
            border.color: Theme.alpha(Theme.warning, 0.45)

            Text {
                id: previewLabel
                anchors.centerIn: parent
                text: qsTr("VISUAL PREVIEW")
                color: Theme.warning
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontNano
                font.letterSpacing: Theme.trackingLabel
            }
        }
    }
}
