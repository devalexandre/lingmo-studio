/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Color page: viewer with before/after split, scopes, lift/gamma/gain wheels,
// curves, and the color effects of the selected clip (HSL qualifier, LUT…).
SplitView {
    id: page
    orientation: Qt.Vertical
    property bool active: false

    readonly property var sel: editor.selectedClip
    readonly property bool hasClip: sel && sel.id !== undefined
    function effectOf(type) {
        if (!hasClip)
            return null
        for (let i = 0; i < sel.effects.length; ++i)
            if (sel.effects[i].type === type)
                return sel.effects[i]
        return null
    }
    readonly property var wheels: effectOf("wheels")
    readonly property var curvesFx: effectOf("curves")
    function w(key, def) { return wheels ? Number(wheels.params[key]) : def }

    // Like a grading suite, the Color page works on the clip under the
    // playhead (top-most visible video track)
    function autoSelect() {
        if (!active)
            return
        const tl = editor.timeline
        const at = editor.player.timelinePosition
        for (let t = 0; t < tl.trackCount; ++t) {
            if (tl.trackKind(t) !== "video")
                continue
            const clips = tl.clipsOf(t)
            for (let i = 0; i < clips.length; ++i) {
                const c = clips[i]
                if (c.kind !== "title" && c.position <= at && c.position + c.duration > at) {
                    if (!(hasClip && sel.id === c.id))
                        editor.select(c.id)
                    return
                }
            }
        }
    }
    Connections {
        target: editor.player
        function onPositionChanged() { if (!editor.player.playing) Qt.callLater(page.autoSelect) }
        function onPlayingChanged() { if (!editor.player.playing) Qt.callLater(page.autoSelect) }
    }
    onActiveChanged: { editor.scopes.enabled = active; autoSelect() }
    Component.onCompleted: editor.scopes.enabled = active
    Connections {
        target: editor
        function onDocumentChanged() { Qt.callLater(page.autoSelect) }
    }

    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 260
        orientation: Qt.Horizontal

        MonitorPanel {
            id: monitor
            SplitView.fillWidth: true
            showSourceControls: false
            compare: compareSwitch.checked
        }

        StudioPanel {
            id: scopes
            title: qsTr("Scopes")
            SplitView.preferredWidth: 420
            SplitView.minimumWidth: 260
            property int kind: 0
            readonly property var kinds: ["waveform", "parade", "vectorscope", "histogram"]
            actions: [
                ComboBox {
                    implicitWidth: 130
                    model: [qsTr("Waveform"), qsTr("RGB parade"), qsTr("Vectorscope"), qsTr("Histogram")]
                    currentIndex: scopes.kind
                    onActivated: function(i) { scopes.kind = i }
                }
            ]
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
                spacing: LingmoUI.Units.smallSpacing
                ScopeView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    kind: scopes.kinds[scopes.kind]
                }
                ScopeView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: parent.height * 0.35
                    kind: scopes.kind === 2 ? "waveform" : "vectorscope"
                }
            }
        }
    }

    StudioPanel {
        SplitView.preferredHeight: 330
        SplitView.minimumHeight: 240
        title: page.hasClip ? qsTr("Grade: %1").arg(page.sel.name || "") : qsTr("Select a clip to grade")
        actions: [
            Switch { id: compareSwitch; text: qsTr("Before / after") },
            LingmoUI.ActionButton { text: qsTr("Copy grade"); enabled: page.hasClip; onClicked: editor.copyGrade(page.sel.id) },
            LingmoUI.ActionButton { text: qsTr("Paste grade"); enabled: editor.hasGrade && editor.selection.length > 0; onClicked: editor.pasteGrade() },
            LingmoUI.ActionButton { text: qsTr("Reset"); destructive: true; subtle: true; enabled: page.hasClip; onClicked: editor.resetGrade(page.sel.id) }
        ]

        RowLayout {
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.smallSpacing
            spacing: LingmoUI.Units.smallSpacing
            enabled: page.hasClip

            // Wheels
            LingmoUI.Card {
                Layout.fillHeight: true
                Layout.preferredWidth: 520
                color: LingmoUI.Theme.backgroundColor
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: LingmoUI.Units.smallSpacing
                    ColorWheel {
                        Layout.fillWidth: true
                        label: qsTr("Lift")
                        neutral: 0
                        range: 0.25
                        r: page.w("lift_r", 0); g: page.w("lift_g", 0); b: page.w("lift_b", 0)
                        onMoved: function(r, g, b) { editor.setWheel(page.sel.id, "lift", r, g, b) }
                    }
                    ColorWheel {
                        Layout.fillWidth: true
                        label: qsTr("Gamma")
                        neutral: 1
                        range: 0.5
                        r: page.w("gamma_r", 1); g: page.w("gamma_g", 1); b: page.w("gamma_b", 1)
                        onMoved: function(r, g, b) { editor.setWheel(page.sel.id, "gamma", r, g, b) }
                    }
                    ColorWheel {
                        Layout.fillWidth: true
                        label: qsTr("Gain")
                        neutral: 1
                        range: 0.5
                        r: page.w("gain_r", 1); g: page.w("gain_g", 1); b: page.w("gain_b", 1)
                        onMoved: function(r, g, b) { editor.setWheel(page.sel.id, "gain", r, g, b) }
                    }
                }
            }

            // Curves
            LingmoUI.Card {
                Layout.fillHeight: true
                Layout.preferredWidth: 300
                color: LingmoUI.Theme.backgroundColor
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: LingmoUI.Units.smallSpacing
                    LingmoUI.SegmentedControl {
                        id: channel
                        Layout.alignment: Qt.AlignHCenter
                        model: ["Y", "R", "G", "B"]
                        readonly property var keys: ["master", "red", "green", "blue"]
                    }
                    CurvesEditor {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        channel: channel.keys[channel.currentIndex]
                        points: page.curvesFx ? page.curvesFx.params[channel.keys[channel.currentIndex]] : "0/0 1/1"
                        onEdited: function(points) {
                            if (!page.curvesFx)
                                editor.addEffect(page.sel.id, "curves")
                            const fx = page.effectOf("curves")
                            if (fx)
                                editor.setEffectParam(page.sel.id, fx.index, channel.keys[channel.currentIndex], points)
                        }
                    }
                }
            }

            // Everything else of the grade (primaries, HSL qualifier, LUT)
            LingmoUI.Card {
                Layout.fillHeight: true
                Layout.fillWidth: true
                color: LingmoUI.Theme.backgroundColor
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: LingmoUI.Units.smallSpacing
                    RowLayout {
                        LingmoUI.ActionButton { text: qsTr("Primaries"); enabled: !page.effectOf("color"); onClicked: editor.addEffect(page.sel.id, "color") }
                        LingmoUI.ActionButton { text: qsTr("HSL qualifier"); onClicked: editor.addEffect(page.sel.id, "hsl") }
                        LingmoUI.ActionButton { text: qsTr("LUT"); onClicked: editor.addEffect(page.sel.id, "lut") }
                    }
                    Inspector {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        categories: ["color"]
                        showClipSection: false
                    }
                }
            }
        }
    }
}
