import QtQuick
import Jarvis.Theme

/// A low-cost audio horizon. The envelope comes from the real microphone or
/// speaker level; the phase timer only runs while audio is active, so an idle
/// JARVIS window remains effectively still.
Item {
    id: wave

    property real level: 0.0
    property bool active: false
    property color tint: Theme.accent
    property real phase: 0.0

    implicitHeight: 120

    Timer {
        interval: 34
        repeat: true
        running: wave.active && wave.visible
        onTriggered: {
            wave.phase = (wave.phase + 0.22) % (Math.PI * 2)
            canvas.requestPaint()
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        function drawLine(ctx, alpha, width, amplitude, offset) {
            ctx.beginPath()
            for (let x = 0; x <= width; x += 4) {
                const ratio = x / Math.max(1, width)
                const envelope = 0.28 + 0.72 * Math.sin(ratio * Math.PI)
                const y = height / 2 + offset
                          + Math.sin(x * 0.030 + wave.phase) * amplitude * envelope
                          + Math.sin(x * 0.073 - wave.phase * 1.37) * amplitude * 0.32 * envelope
                if (x === 0) ctx.moveTo(x, y)
                else ctx.lineTo(x, y)
            }
            ctx.strokeStyle = Theme.alpha(wave.tint, alpha)
            ctx.lineWidth = width
            ctx.stroke()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            const energy = Math.max(0.0, Math.min(1.0, wave.level * 8.0))
            const amplitude = wave.active
                              ? Math.max(1.0, height * (0.012 + energy * 0.34))
                              : 0.0

            // The baseline remains visible when idle, just like the reference
            // HUD. During speech the two glow passes make the line feel alive
            // without becoming a heavy particle effect.
            drawLine(ctx, wave.active ? 0.10 : 0.16, 5, amplitude * 1.55, 0)
            drawLine(ctx, wave.active ? 0.22 : 0.24, 2, amplitude * 1.16, 0)
            drawLine(ctx, wave.active ? 0.86 : 0.52, 1, amplitude, 0)
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
    }

    onLevelChanged: canvas.requestPaint()
    onActiveChanged: canvas.requestPaint()
    onTintChanged: canvas.requestPaint()
}
