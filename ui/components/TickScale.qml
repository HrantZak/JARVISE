import QtQuick
import Jarvis.Theme

/// Horizontal ruler of fine ticks.
///
/// The signature rule of the interface: it separates regions the way a hairline
/// would, but carries the same measured, instrument-like texture as the ring
/// around the AI Core. Used instead of plain dividers throughout.
///
/// Painted into a Canvas rather than assembled from a Repeater of Rectangles.
/// A single rule across a 1280 px window is over a hundred ticks, and three of
/// them put more than three hundred items into the scene graph - all of which
/// then re-evaluate their bindings while the page is being torn down. One
/// texture costs nothing to keep and nothing to destroy.
Item {
    id: scale

    property color tint: Theme.accent
    property int tickSpacing: 9
    property int majorEvery: 5
    property real minorHeight: 3
    property real majorHeight: 7
    property real baseOpacity: 0.30

    /// Draw the ticks hanging from the bottom edge instead of the top.
    property bool flipped: false

    implicitHeight: majorHeight + 1

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            const r = scale.tint.r
            const g = scale.tint.g
            const b = scale.tint.b

            // Baseline rule.
            ctx.strokeStyle = Qt.rgba(r, g, b, scale.baseOpacity * 0.5)
            ctx.lineWidth = 1
            const baseY = scale.flipped ? height - 0.5 : 0.5
            ctx.beginPath()
            ctx.moveTo(0, baseY)
            ctx.lineTo(width, baseY)
            ctx.stroke()

            // Ticks.
            const count = Math.floor(width / scale.tickSpacing)
            for (let i = 0; i <= count; ++i) {
                const major = (i % scale.majorEvery) === 0
                const length = major ? scale.majorHeight : scale.minorHeight
                const x = i * scale.tickSpacing + 0.5

                ctx.strokeStyle = Qt.rgba(r, g, b,
                                          major ? scale.baseOpacity
                                                : scale.baseOpacity * 0.45)
                ctx.beginPath()
                if (scale.flipped) {
                    ctx.moveTo(x, height)
                    ctx.lineTo(x, height - length)
                } else {
                    ctx.moveTo(x, 0)
                    ctx.lineTo(x, length)
                }
                ctx.stroke()
            }
        }
    }

    // Repaint only when something geometric or chromatic actually changes.
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onTintChanged: canvas.requestPaint()
    onBaseOpacityChanged: canvas.requestPaint()
    onTickSpacingChanged: canvas.requestPaint()
    onFlippedChanged: canvas.requestPaint()
}
