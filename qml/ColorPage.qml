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

// Color page: viewer with scopes beside it, the program's clips as a node
// strip, and the grading tools below in tabs: colour wheels (lift, gamma,
// gain, offset), curves, the HSL qualifier, primaries and LUTs.
SplitView {
    id: page
    orientation: Qt.Vertical
    handle: PanelHandle { }
    property bool active: false
    property int tab: 0

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
    // offset = the black level the lift/gain pair adds (gain × lift)
    function off(c) { return wheels ? Number(wheels.params["gain_" + c]) * Number(wheels.params["lift_" + c]) : 0 }

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

    // ---------------------------------------------------- viewer | scopes
    SplitView {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 240
        orientation: Qt.Horizontal
        handle: PanelHandle { }

        MonitorPanel {
            id: monitor
            SplitView.fillWidth: true
            SplitView.minimumWidth: 400
            showSourceControls: false
            compare: compareButton.checked
        }

        StudioPanel {
            id: scopes
            title: qsTr("Scopes")
            icon: "scopes"
            SplitView.preferredWidth: 400
            SplitView.minimumWidth: 260
            property int kind: 0
            readonly property var kinds: ["waveform", "parade", "vectorscope", "histogram"]
            actions: [
                Row {
                    spacing: 1
                    Repeater {
                        model: [qsTr("Waveform"), qsTr("Parade"), qsTr("Vector"), qsTr("Histogram")]
                        StudioButton {
                            text: modelData
                            size: 22
                            fontSize: StudioTheme.fontTiny
                            checkable: true
                            checked: scopes.kind === index
                            onClicked: scopes.kind = index
                        }
                    }
                }
            ]
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                ScopeView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    kind: scopes.kinds[scopes.kind]
                }
                ScopeView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: parent.height * 0.38
                    kind: scopes.kind === 2 ? "waveform" : "vectorscope"
                }
            }
        }
    }

    // ---------------------------------------------------- clips as nodes
    ClipStrip {
        SplitView.preferredHeight: 104
        SplitView.minimumHeight: 104
        SplitView.maximumHeight: 104
    }

    // ---------------------------------------------------- grading tools
    StudioPanel {
        SplitView.preferredHeight: 330
        SplitView.minimumHeight: 250
        title: page.hasClip ? (page.sel.name || qsTr("Clip")) : qsTr("Select a clip to grade")
        leading: [
            Row {
                spacing: 2
                Layout.leftMargin: 10
                Repeater {
                    model: [qsTr("Color Wheels"), qsTr("Curves"), qsTr("Qualifier"), qsTr("Primaries & LUT")]
                    StudioButton {
                        text: modelData
                        size: 24
                        fontSize: StudioTheme.fontSmall
                        checkable: true
                        checked: page.tab === index
                        onClicked: page.tab = index
                    }
                }
            }
        ]
        actions: [
            StudioButton { id: compareButton; iconName: "compare"; text: qsTr("Before / after"); size: 24; fontSize: StudioTheme.fontSmall; checkable: true },
            StudioButton { text: qsTr("Copy grade"); size: 24; fontSize: StudioTheme.fontSmall; enabled: page.hasClip; onClicked: editor.copyGrade(page.sel.id) },
            StudioButton { text: qsTr("Paste grade"); size: 24; fontSize: StudioTheme.fontSmall; enabled: editor.hasGrade && editor.selection.length > 0; onClicked: editor.pasteGrade() },
            StudioButton { iconName: "reset"; tip: qsTr("Reset grade"); size: 24; enabled: page.hasClip; onClicked: editor.resetGrade(page.sel.id) }
        ]

        StackLayout {
            anchors.fill: parent
            anchors.margins: 8
            currentIndex: page.tab
            enabled: page.hasClip

            // wheels
            RowLayout {
                spacing: 18
                Item { Layout.fillWidth: true; Layout.maximumWidth: 40 }
                ColorWheel {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    label: qsTr("Lift")
                    neutral: 0
                    range: 0.25
                    r: page.w("lift_r", 0); g: page.w("lift_g", 0); b: page.w("lift_b", 0)
                    onMoved: function(r, g, b) { editor.setWheel(page.sel.id, "lift", r, g, b) }
                }
                ColorWheel {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    label: qsTr("Gamma")
                    neutral: 1
                    range: 0.5
                    r: page.w("gamma_r", 1); g: page.w("gamma_g", 1); b: page.w("gamma_b", 1)
                    onMoved: function(r, g, b) { editor.setWheel(page.sel.id, "gamma", r, g, b) }
                }
                ColorWheel {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    label: qsTr("Gain")
                    neutral: 1
                    range: 0.5
                    r: page.w("gain_r", 1); g: page.w("gain_g", 1); b: page.w("gain_b", 1)
                    onMoved: function(r, g, b) { editor.setWheel(page.sel.id, "gain", r, g, b) }
                }
                ColorWheel {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    label: qsTr("Offset")
                    neutral: 0
                    range: 0.2
                    r: page.off("r"); g: page.off("g"); b: page.off("b")
                    onMoved: function(r, g, b) { editor.setWheelOffset(page.sel.id, r, g, b) }
                }
                Item { Layout.fillWidth: true; Layout.maximumWidth: 40 }
            }

            // curves
            RowLayout {
                id: curvesRow
                spacing: 12
                ColumnLayout {
                    Layout.preferredWidth: 120
                    Layout.fillHeight: true
                    spacing: 4
                    Repeater {
                        model: [{ k: "master", t: qsTr("Luma"), c: StudioTheme.text }, { k: "red", t: qsTr("Red"), c: "#FF6B6B" },
                                { k: "green", t: qsTr("Green"), c: "#5FD97A" }, { k: "blue", t: qsTr("Blue"), c: "#6B9BFF" }]
                        StudioButton {
                            Layout.fillWidth: true
                            text: modelData.t
                            checkable: true
                            checked: curves.channel === modelData.k
                            checkedColor: modelData.c
                            onClicked: curves.channel = modelData.k
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
                Rectangle {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.minimumWidth: 240
                    Layout.maximumWidth: 560
                    radius: StudioTheme.radius
                    color: StudioTheme.well
                    CurvesEditor {
                        id: curves
                        anchors.fill: parent
                        anchors.margins: 8
                        channel: "master"
                        points: page.curvesFx ? page.curvesFx.params[channel] : "0/0 1/1"
                        onEdited: function(points) {
                            if (!page.curvesFx)
                                editor.addEffect(page.sel.id, "curves")
                            const fx = page.effectOf("curves")
                            if (fx)
                                editor.setEffectParam(page.sel.id, fx.index, curves.channel, points)
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // qualifier
            RowLayout {
                spacing: 12
                ColumnLayout {
                    Layout.preferredWidth: 260
                    Layout.fillHeight: true
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Pick a range of hues and change only those: shift the hue, saturate or lighten them.")
                        color: StudioTheme.textSecondary
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontSmall
                    }
                    StudioButton {
                        text: qsTr("Add HSL qualifier")
                        iconName: "plus"
                        flat: false
                        visible: !page.effectOf("hsl")
                        onClicked: editor.addEffect(page.sel.id, "hsl")
                    }
                    Item { Layout.fillHeight: true }
                }
                Inspector {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    categories: ["color"]
                    types: ["hsl"]
                    showClipSection: false
                    showEffectsHeader: false
                }
            }

            // primaries and LUT
            RowLayout {
                spacing: 12
                ColumnLayout {
                    Layout.preferredWidth: 260
                    Layout.fillHeight: true
                    spacing: 6
                    StudioButton { text: qsTr("Primaries"); iconName: "plus"; flat: false; enabled: !page.effectOf("color"); onClicked: editor.addEffect(page.sel.id, "color") }
                    StudioButton { text: qsTr("LUT"); iconName: "plus"; flat: false; onClicked: editor.addEffect(page.sel.id, "lut") }
                    Item { Layout.fillHeight: true }
                }
                Inspector {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    categories: ["color"]
                    types: ["color", "lut", "wheels", "curves"]
                    showClipSection: false
                    showEffectsHeader: false
                }
            }
        }
    }
}
