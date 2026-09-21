# The JARVIS visual language

The interface is not a dashboard, not a platform shell and not a stock sci-fi
HUD. This document is the rulebook that keeps it that way.

## Principles

**Instrument, not application.** The reference is a measuring device: hairlines,
tick scales, segmented bars, numbers that do not reflow. Panels are bezels, not
cards - no fills, no shadows, no rounded chrome.

**One accent, used sparingly.** Cyan `#3ACBF5` carries every meaning. Colour is
scarce so that when the Core turns amber it registers immediately. Nothing is
coloured for decoration.

**Structure by line, not by box.** Regions are separated by tick rules and
corner brackets. There is no border for its own sake and no boxed grid, because
a grid is exactly what makes an interface read as a dashboard.

**Motion decelerates and never overshoots.** One easing family. The Core
breathes on a 2.6 s cycle; nothing else in the interface moves that slowly, and
that contrast is what makes it feel alive rather than busy.

**Emptiness is part of the composition.** The Core sits at the optical centre
with a horizon rule behind it and wings at the edges. Space is left empty
deliberately.

**Nothing on screen lies.** Unavailable metrics show as unavailable, never as
zero. Destinations without a backend say NOT AVAILABLE and name the phase that
delivers them.

## Typography

| Role | Face | Why |
|---|---|---|
| Display, labels, headings | **Bahnschrift** | The DIN-derived technical face that ships with Windows. Neither Segoe-generic nor a novelty font |
| Body text | **Segoe UI** | Reads well at small sizes without drawing attention |
| All numbers and paths | **Cascadia Mono** | Readings must not reflow as digits change |

Micro labels are uppercase with wide letter spacing (`trackingWide`, 3.2 px).
That one typographic move does most of the work of making the interface read as
an instrument panel.

## Tokens

Everything lives in `ui/themes/Theme.qml` as a QML singleton: surfaces,
accents, text colours, seven type sizes, four tracking values, a spacing scale,
three radii, five durations and three easing curves. No component is allowed a
hard-coded literal.

Two helper functions carry state semantics:

- `coreColor(stateName)` - the hue for each of the eight Core states. Offline is
  deliberately desaturated so a disconnected engine can never be mistaken for a
  working one.
- `loadColor(percent)` - calm below 70%, amber to 90%, red beyond.

## The AI Core

Eight states, each with its own behaviour rather than its own colour alone:

| State | Behaviour |
|---|---|
| OFFLINE | Broken ring: four arcs with wide gaps. Dim, desaturated, **completely still** |
| IDLE | Continuous ring, slow breath (2.6 s), tick ring drifting at 52 s per turn |
| LISTENING | Ring radius follows the microphone envelope; pulses travel outward |
| THINKING | Two arc groups counter-rotating at 3.2 s and 5.2 s |
| EXECUTING | A real progress arc bound to `progress`, plus a fast sweeping head |
| SPEAKING | Tick lengths modulate with the speech envelope - the ring becomes a waveform |
| WARNING | Amber; the disc breathes brightness rather than size |
| ERROR | Red; a short, irregular tremor. Reads as a fault, not as decoration |

The Core is driven entirely by properties on `AiCoreModel`: `state`,
`inputLevel`, `outputLevel`, `progress`. Phase 3 onwards sets them from C++ and
the visuals follow without touching QML.

## Drawing techniques

| Need | Technique | Why not the obvious thing |
|---|---|---|
| Arcs, rings, progress | `QtQuick.Shapes` with `CurveRenderer` | GPU-rendered, one node per arc |
| History graphs | `Shape` + `PathPolyline` | Qt Charts requires QApplication and looks like a business dashboard |
| Tick rings and rules | `Canvas`, painted once, then transformed | A Repeater of ticks put 300+ items in the scene graph and re-evaluated every binding during page teardown |
| Radial glow | `RadialGradient` fill on a Shape | No blur pass, no offscreen buffer |
| Depth | `QtQuick.Particles`, ~30 motes, near-zero velocity | Atmosphere, not a particle demo |

## Motion budget

- **One QTimer in the entire application**, in `SystemMonitor`, at 1 Hz. It
  drives telemetry and the clock. There is no second timer anywhere.
- Every animation binds `running:` to the state that needs it, so exactly one
  set of animators is live and OFFLINE runs none.
- Telemetry stops entirely when the window is minimised.
- Rotation uses `RotationAnimator`, which runs on the render thread rather than
  waking the GUI thread each frame.

Measured on the reference machine, Release build: **8.4% of one core** with the
Core animating, particles running and telemetry polling; **0.4%** with the
window idle on a static page.

## Component inventory

`Jarvis.Components` holds seventeen reusable pieces - `AiCore`, `CoreArc`,
`TickRing`, `TickScale`, `GlassPanel`, `Sparkline`, `MetricReadout`,
`HudMetric`, `StatusChip`, `NavRail`, `PageScaffold`, `HudButton`,
`ToggleSwitch`, `OptionSelector`, `InfoRow`, `TitleBar`, `ParticleField`.

None of them reads an application singleton. Anything that needs `App`, `Core`
or `Sys` lives in `Jarvis.Pages` or `Jarvis.Ui`, which keeps the component
library reusable and independently testable.

## Rules that are enforced by the build

`QtWidgets` and `QtCharts` are not merely discouraged: the configure step fails
if any target links them. See `cmake/JarvisHelpers.cmake`.

## Traps worth remembering

`QQuickItem` declares `left`, `right`, `top`, `bottom`, `baseline` and friends
as FINAL anchor-line properties, and `Item` declares `state`. A custom property
with any of those names fails at QML load time with "Cannot override FINAL
property". Every one of them was hit at least once while building this phase.
