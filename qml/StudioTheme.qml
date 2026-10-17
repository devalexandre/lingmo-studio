/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

pragma Singleton

import QtQuick

// Lingmo Studio's workspace palette. Editing and grading need a dim, neutral
// surround, so the studio is charcoal in both system themes; the accent is
// Lingmo blue. Main.qml hands these values to LingmoUI.Theme so the shared
// components (cards, buttons, menus) follow them too.
QtObject {
    // ---- surfaces
    readonly property color window: "#141518"
    readonly property color panel: "#1C1D21"
    readonly property color raised: "#24262B"
    readonly property color raisedHover: "#2C2E34"
    readonly property color pressed: "#33363D"
    readonly property color well: "#101113"        // viewers, scopes, wells
    readonly property color border: "#2E3036"
    readonly property color divider: "#26282D"

    // ---- text
    readonly property color text: "#E6E7EA"
    readonly property color textSecondary: "#9A9DA6"
    readonly property color textDisabled: "#5E616A"

    // ---- accent (Lingmo blue)
    readonly property color accent: "#2779FF"
    readonly property color accentHover: "#4A8FFF"
    readonly property color accentSoft: Qt.rgba(0.153, 0.475, 1.0, 0.16)
    readonly property color accentText: "#7FB0FF"

    // ---- timeline
    readonly property color videoClip: "#34507A"
    readonly property color videoClipEdge: "#5578AE"
    readonly property color audioClip: "#245A41"
    readonly property color audioClipEdge: "#3E8B63"
    readonly property color audioWave: "#8FE0B2"
    readonly property color captionClip: "#6E5E1E"
    readonly property color captionClipEdge: "#D9B83A"
    readonly property color titleClip: "#4E3F7A"
    readonly property color titleClipEdge: "#8A74C9"
    readonly property color compoundClip: "#6E4A24"
    readonly property color compoundClipEdge: "#C88A45"
    readonly property color selection: "#F2F3F5"
    readonly property color playhead: "#F04A3E"
    readonly property color snap: "#F5C542"
    readonly property color trackVideo: "#4D7FD1"
    readonly property color trackAudio: "#3FAF74"
    readonly property color trackCaption: "#D8B43A"
    readonly property color laneEven: "#18191D"
    readonly property color laneOdd: "#1A1B1F"

    // ---- states
    readonly property color mute: "#E0603F"
    readonly property color solo: "#E9C23A"
    readonly property color record: "#E5484D"
    readonly property color success: "#3FAF74"
    readonly property color warning: "#E9A23B"
    readonly property color danger: "#E5484D"

    // ---- shape and rhythm
    readonly property int radius: 8
    readonly property int smallRadius: 4
    readonly property int gap: 6            // between panels
    readonly property int pad: 10           // inside panels
    readonly property int headerHeight: 30  // panel headers, tool rows
    readonly property int controlHeight: 28
    readonly property int fast: 120         // hover, press
    readonly property int normal: 160       // panels, page switches

    // ---- type (Inter, pixel sizes: dense but readable)
    readonly property string font: "Inter"
    readonly property int fontTiny: 10
    readonly property int fontSmall: 11
    readonly property int fontBody: 12
    readonly property int fontLarge: 13
    readonly property int fontTitle: 16
    readonly property int fontHero: 24

    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    // own monochrome icons (data/icons), tinted by StudioIcon
    function icon(name) { return "qrc:/data/icons/" + name + ".svg" }
    // "Tip (B)": tooltips carry the shortcut
    function tip(text, shortcut) { return shortcut && shortcut.length ? text + "  (" + shortcut + ")" : text }
}
