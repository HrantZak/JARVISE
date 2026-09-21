pragma Singleton

import QtQuick

/// The JARVIS design system.
///
/// Every colour, size, duration and easing curve the interface draws with is
/// defined here. Nothing downstream is allowed a hard-coded literal, so the
/// visual language can be tuned in one file.
///
/// The language, briefly: near-black ground, one saturated blue accent that carries all
/// meaning, hairline structure instead of boxes, wide-tracked technical
/// lettering, and motion that is slow and eased rather than bouncy. Colour is
/// scarce on purpose - when the Core turns amber, that has to mean something.
QtObject {
    id: theme

    // =====================================================================
    // Surfaces
    // =====================================================================

    readonly property color backgroundDeep:   "#000000"
    readonly property color backgroundBase:   "#000000"
    readonly property color backgroundRaised: "#02050B"
    readonly property color backgroundInset:  "#000000"

    readonly property color glassFill:        Qt.rgba(0.01, 0.03, 0.08, 0.55)
    readonly property color glassFillStrong:  Qt.rgba(0.02, 0.05, 0.12, 0.80)
    readonly property color glassStroke:      Qt.rgba(0.08, 0.24, 0.52, 0.26)
    readonly property color glassStrokeLit:   Qt.rgba(0.12, 0.46, 1.00, 0.52)
    readonly property color hairline:         Qt.rgba(0.10, 0.30, 0.70, 0.24)
    readonly property color scrim:            Qt.rgba(0.00, 0.00, 0.00, 0.86)

    // =====================================================================
    // Accents
    // =====================================================================

    readonly property color accent:           "#2E83FF"
    readonly property color accentBright:     "#65D5FF"
    readonly property color accentDeep:       "#1455B5"

    readonly property color warning:          "#F2B24B"
    readonly property color danger:           "#FF5F73"
    readonly property color success:          "#54E0A6"
    readonly property color offline:          "#4E6A7C"

    // =====================================================================
    // Text
    // =====================================================================

    readonly property color textPrimary:      "#E0ECFF"
    readonly property color textSecondary:    "#A8C1E8"
    readonly property color textMuted:        "#7089B4"
    readonly property color textDim:          "#4C6590"

    // =====================================================================
    // Typography
    //
    // Bahnschrift is the technical, DIN-derived face that ships with Windows;
    // it gives JARVIS a voice that is neither Segoe-generic nor a novelty
    // sci-fi font. Cascadia Mono carries every number, because readings must
    // never reflow as digits change.
    // =====================================================================

    readonly property string displayFamily:   "Bahnschrift"
    readonly property string bodyFamily:      "Segoe UI"
    readonly property string monoFamily:      "Cascadia Mono"

    readonly property int fontHero:           44
    readonly property int fontDisplay:        28
    readonly property int fontTitle:          19
    readonly property int fontSubtitle:       15
    readonly property int fontBody:           14
    readonly property int fontSmall:          12
    readonly property int fontMicro:          10
    readonly property int fontNano:           9

    readonly property real trackingHero:      6.0
    readonly property real trackingWide:      3.2
    readonly property real trackingLabel:     1.7
    readonly property real trackingBody:      0.2

    // =====================================================================
    // Spacing and metrics
    // =====================================================================

    readonly property int spacingXxs:         2
    readonly property int spacingXs:          4
    readonly property int spacingSm:          8
    readonly property int spacingMd:          16
    readonly property int spacingLg:          24
    readonly property int spacingXl:          36
    readonly property int spacingXxl:         56

    readonly property int radiusSm:           3
    readonly property int radiusMd:           6
    readonly property int radiusLg:           12

    readonly property int titleBarHeight:     48
    readonly property int hudHeight:          74
    // Sized for the longest navigation label in any supported language:
    // АВТОМАТИЗАЦИЯ is half again as wide as AUTOMATION.
    readonly property int navWidth:           214
    readonly property int navItemHeight:      40
    readonly property int borderWidth:        1

    // Label column in InfoRow. Sized for the longest label in any supported
    // language - СИСТЕМНЫЕ ИНСТРУМЕНТЫ is more than twice the width of
    // SYSTEM TOOLS once the wide tracking is applied.
    readonly property int labelColumnWidth:   214

    // =====================================================================
    // Motion
    //
    // One family of curves. Everything decelerates; nothing overshoots.
    // =====================================================================

    readonly property int durationInstant:    90
    readonly property int durationFast:       160
    readonly property int durationNormal:     260
    readonly property int durationSlow:       420
    readonly property int durationCinematic:  760

    readonly property int easeStandard:       Easing.OutCubic
    readonly property int easeEmphasis:       Easing.OutQuint
    readonly property int easeBreath:         Easing.InOutSine

    // =====================================================================
    // AI Core state palette
    //
    // Each state owns a hue. Offline is deliberately desaturated so a
    // disconnected engine can never be mistaken for a working one.
    // =====================================================================

    function coreColor(stateName) {
        switch (stateName) {
        case "IDLE":       return accent
        case "LISTENING":  return "#5BE1C4"
        case "THINKING":   return "#7C9BFF"
        // Planning is deliberation, so it sits next to THINKING rather than
        // borrowing the brighter colour that means "something is happening to
        // the machine".
        case "PLANNING":   return "#9B8CFF"
        case "EXECUTING":  return accentBright
        // Waiting on a person. Warm, because it is the one state that will not
        // resolve on its own.
        case "CONFIRMING": return warning
        case "RECOVERING": return "#F29B4B"
        case "SPEAKING":   return "#63D8FF"
        case "WARNING":    return warning
        case "ERROR":      return danger
        case "OFFLINE":    return offline
        }
        return offline
    }

    /// Colour for a load percentage: calm until it is genuinely high.
    function loadColor(percent) {
        if (percent >= 90) return danger
        if (percent >= 70) return warning
        return accent
    }

    /// Same colour at a different opacity. Deliberately not defensive: an
    /// undefined argument should surface as a loud TypeError, not be swallowed
    /// into a transparent pixel nobody notices.
    function alpha(baseColor, a) {
        return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, a)
    }
}
