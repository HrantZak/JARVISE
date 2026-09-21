import QtQuick
import QtQuick.Particles
import Jarvis.Theme

/// Slow drift of fine motes, for depth behind the AI Core.
///
/// Deliberately restrained: a low emission rate, long lifetimes and near-zero
/// velocity. The intent is atmosphere - the sense of looking into a volume
/// rather than at a flat panel - not a particle demo. The whole system stops
/// when `active` goes false, so a hidden or minimised window costs nothing.
Item {
    id: field

    property bool active: true
    property color tint: Theme.accent
    property int density: 34

    ParticleSystem {
        id: system
        anchors.fill: parent
        running: field.active && field.visible
        paused: !field.active
    }

    ImageParticle {
        system: system
        anchors.fill: parent
        color: Theme.alpha(field.tint, 0.30)
        colorVariation: 0.25
        alpha: 0
        entryEffect: ImageParticle.Fade
    }

    Emitter {
        system: system
        anchors.fill: parent
        emitRate: field.active ? field.density / 8 : 0
        lifeSpan: 9000
        lifeSpanVariation: 3000
        size: 2
        sizeVariation: 1.5
        endSize: 1

        velocity: AngleDirection {
            angle: 270
            angleVariation: 40
            magnitude: 5
            magnitudeVariation: 4
        }
    }
}
