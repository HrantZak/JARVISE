import QtQuick
import Jarvis.Theme

/// Ring of fine radial ticks, with every Nth tick longer.
///
/// Painted once into a Canvas and then rotated as a whole: rotation is a GPU
/// transform, so a spinning ring costs nothing per frame. Repainting happens
/// only when a geometric property changes, never on animation.
Item {
    id: ring

    property color tint: Theme.accent
    property int tickCount: 90
    property int majorEvery: 6
    property real tickLength: 7
    property real majorTickLength: 13
    property real thickness: 1
    property real radiusRatio: 0.5
    property real tickOpacity: 0.55

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            const cx = width / 2
            const cy = height / 2
            const outer = Math.min(width, height) * ring.radiusRatio

            ctx.lineWidth = ring.thickness
            ctx.lineCap = "butt"

            for (let i = 0; i < ring.tickCount; ++i) {
                const major = (i % ring.majorEvery) === 0
                const length = major ? ring.majorTickLength : ring.tickLength
                const angle = (i / ring.tickCount) * Math.PI * 2 - Math.PI / 2

                ctx.strokeStyle = Qt.rgba(ring.tint.r, ring.tint.g, ring.tint.b,
                                          major ? ring.tickOpacity
                                                : ring.tickOpacity * 0.45)
                ctx.beginPath()
                ctx.moveTo(cx + Math.cos(angle) * (outer - length),
                           cy + Math.sin(angle) * (outer - length))
                ctx.lineTo(cx + Math.cos(angle) * outer,
                           cy + Math.sin(angle) * outer)
                ctx.stroke()
            }
        }
    }

    // Repaint only on real geometry or colour changes.
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onTintChanged: canvas.requestPaint()
    onTickCountChanged: canvas.requestPaint()
    onTickOpacityChanged: canvas.requestPaint()
    onRadiusRatioChanged: canvas.requestPaint()
}
