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

// Multi-track timeline: ruler, track headers, clips, playhead, markers.
// Edits are previewed while dragging and committed to the editor on release.
StudioPanel {
    id: panel

    property real ppf: 2.0 // pixels per frame
    property bool showToolbar: true
    property bool compact: false
    readonly property int headerWidth: compact ? 120 : 176
    readonly property var model: editor.timeline
    readonly property var player: editor.player
    readonly property string tool: root.tool
    readonly property bool snapping: root.snapping

    function trackHeight(kind) {
        if (kind === "subtitle")
            return 34
        if (kind === "audio")
            return compact ? 64 : 54
        return compact ? 44 : 70
    }
    function xOf(frame) { return frame * ppf }
    function frameOf(contentX) { return Math.max(0, Math.round(contentX / ppf)) }
    function fit() {
        const frames = Math.max(model.duration, Math.round(editor.fps * 10))
        ppf = Math.max(0.02, (flick.width - 60) / frames)
    }
    property bool userZoomed: false
    function zoom(factor, anchorX) {
        userZoomed = true
        const frame = (flick.contentX + anchorX) / ppf
        ppf = Math.max(0.02, Math.min(40, ppf * factor))
        flick.contentX = Math.max(0, frame * ppf - anchorX)
    }
    function snap(frame, ignoreClip) {
        if (!snapping)
            return frame
        return editor.snapFrame(frame, Math.max(1, Math.round(10 / ppf)), ignoreClip || "")
    }
    // track index under a y in lane-column coordinates
    function trackAt(y) {
        let acc = 0
        for (let i = 0; i < lanes.count; ++i) {
            const h = lanes.itemAt(i) ? lanes.itemAt(i).height : 0
            if (y < acc + h)
                return i
            acc += h
        }
        return lanes.count - 1
    }
    function clipEndingAt(track, frame) {
        const clips = model.clipsOf(track)
        for (let i = 0; i < clips.length; ++i)
            if (clips[i].position + clips[i].duration === frame)
                return clips[i].id
        return ""
    }

    Component.onCompleted: Qt.callLater(fit)
    // hidden pages get their real size later: keep fitting until the user zooms
    Connections {
        target: flick
        function onWidthChanged() { if (!panel.userZoomed && flick.width > 100) Qt.callLater(panel.fit) }
    }
    Connections {
        target: panel.model
        function onChanged() { if (!panel.userZoomed && panel.xOf(panel.model.duration) > flick.width) Qt.callLater(panel.fit) }
    }
    Connections {
        target: editor
        function onProjectChanged() { panel.userZoomed = false; Qt.callLater(panel.fit) }
        function onSequenceChanged() { Qt.callLater(panel.fit) }
    }
    // keep the playhead in view while playing
    Connections {
        target: player
        function onPositionChanged() {
            if (!player.playing || player.mode !== "timeline")
                return
            const x = panel.xOf(player.timelinePosition)
            if (x > flick.contentX + flick.width * 0.92 || x < flick.contentX)
                flick.contentX = Math.max(0, x - flick.width * 0.1)
        }
    }

    // ------------------------------------------------------------ menus
    Menu {
        id: clipMenu
        property var target: ({})
        property int frame: 0
        MenuItem { text: qsTr("Split here"); onTriggered: editor.splitClip(clipMenu.target.id, clipMenu.frame) }
        MenuItem { text: qsTr("Delete"); onTriggered: { editor.select(clipMenu.target.id); editor.deleteSelected(false) } }
        MenuItem { text: qsTr("Ripple delete"); onTriggered: { editor.select(clipMenu.target.id); editor.deleteSelected(true) } }
        MenuSeparator {}
        Menu {
            title: qsTr("Transition in")
            Repeater {
                model: editor.transitionTypes
                MenuItem {
                    text: panel.transitionName(modelData)
                    onTriggered: editor.setTransition(clipMenu.target.id, Math.max(clipMenu.target.transitionIn, Math.round(editor.fps / 2)), modelData)
                }
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Remove transition"); enabled: clipMenu.target.transitionIn > 0; onTriggered: editor.setTransition(clipMenu.target.id, 0, "") }
        }
        Menu {
            title: qsTr("Speed")
            Repeater {
                model: [0.25, 0.5, 1, 1.5, 2, 4, -1]
                MenuItem {
                    text: modelData < 0 ? qsTr("Reverse") : (modelData + "×")
                    onTriggered: editor.setClipProperty(clipMenu.target.id, "speed", modelData)
                }
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Speed ramp: slow → fast")
                onTriggered: editor.setSpeedRamp(clipMenu.target.id, [{ frame: 0, speed: 0.5 }, { frame: clipMenu.target.duration - 1, speed: 2 }])
            }
            MenuItem {
                text: qsTr("Speed ramp: fast → slow")
                onTriggered: editor.setSpeedRamp(clipMenu.target.id, [{ frame: 0, speed: 2 }, { frame: clipMenu.target.duration - 1, speed: 0.5 }])
            }
            MenuItem { text: qsTr("Remove speed ramp"); enabled: clipMenu.target.ramp === true; onTriggered: editor.setSpeedRamp(clipMenu.target.id, []) }
        }
        MenuItem { text: qsTr("Freeze frame here"); enabled: clipMenu.target.kind === "media"; onTriggered: { player.seek(clipMenu.frame); editor.select(clipMenu.target.id); editor.freezeFrame() } }
        MenuItem { text: qsTr("Detach audio (for J/L cuts)"); enabled: clipMenu.target.hasAudio === true && clipMenu.target.kind === "media"; onTriggered: editor.detachAudio(clipMenu.target.id) }
        MenuItem { text: qsTr("Unlink"); enabled: clipMenu.target.linked === true; onTriggered: editor.setClipProperty(clipMenu.target.id, "unlink", true) }
        MenuItem { text: qsTr("Make compound clip"); onTriggered: { if (editor.selection.indexOf(clipMenu.target.id) < 0) editor.select(clipMenu.target.id); editor.createCompound() } }
        MenuItem { text: qsTr("Open compound clip"); visible: clipMenu.target.kind === "compound"; height: visible ? implicitHeight : 0; onTriggered: editor.openCompound(clipMenu.target.id) }
        MenuSeparator {}
        MenuItem { text: qsTr("Copy grade"); onTriggered: editor.copyGrade(clipMenu.target.id) }
        MenuItem { text: qsTr("Paste grade"); enabled: editor.hasGrade; onTriggered: { editor.select(clipMenu.target.id); editor.pasteGrade() } }
        MenuSeparator {}
        MenuItem { text: qsTr("Normalize loudness (-14 LUFS)"); enabled: clipMenu.target.hasAudio === true; onTriggered: editor.normalizeLoudness(clipMenu.target.id, -14) }
        MenuItem { text: qsTr("Cut out silence"); enabled: clipMenu.target.hasAudio === true; onTriggered: editor.splitOnSilence(clipMenu.target.id, -35, 0.6, true) }
        MenuItem { text: qsTr("Split at scene changes"); enabled: clipMenu.target.mediaKind === "video"; onTriggered: editor.splitOnScenes(clipMenu.target.id, 0.35) }
        MenuItem {
            text: editor.whisperAvailable ? qsTr("Automatic subtitles") : qsTr("Automatic subtitles (install whisper.cpp)")
            enabled: editor.whisperAvailable && clipMenu.target.hasAudio === true
            onTriggered: editor.autoSubtitles(clipMenu.target.id, "")
        }
    }
    function openClipMenu(info, frame) {
        clipMenu.target = info
        clipMenu.frame = frame
        clipMenu.popup()
    }
    function transitionName(type) { return root.transitionLabel(type) }

    Menu {
        id: laneMenu
        property int track: 0
        property int frame: 0
        MenuItem { text: qsTr("Close gap"); onTriggered: editor.removeGap(laneMenu.track, laneMenu.frame) }
        MenuItem { text: qsTr("Add marker here"); onTriggered: editor.addMarker(laneMenu.frame) }
        MenuSeparator {}
        MenuItem { text: qsTr("Add video track"); onTriggered: editor.addTrack("video") }
        MenuItem { text: qsTr("Add audio track"); onTriggered: editor.addTrack("audio") }
        MenuItem { text: qsTr("Add subtitle track"); onTriggered: editor.addTrack("subtitle") }
        MenuItem { text: qsTr("Remove this track"); onTriggered: editor.removeTrack(laneMenu.track) }
    }

    Dialog {
        id: markerDialog
        property int index: -1
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Marker")
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: TextField { id: markerName; selectByMouse: true }
        onAccepted: editor.renameMarker(index, markerName.text)
    }

    // ------------------------------------------------------------ toolbar
    actions: [
        RowLayout {
            visible: panel.showToolbar
            spacing: 2
            Repeater {
                model: editor.sequencePath.length > 1 ? editor.sequencePath : []
                LingmoUI.ActionButton {
                    text: modelData
                    subtle: true
                    enabled: index < editor.sequencePath.length - 1
                    onClicked: editor.leaveCompound(index)
                }
            }
            LingmoUI.IconButton { iconName: "edit-select"; tipText: qsTr("Select and move (A)"); checkable: true; checked: root.tool === "select"; onClicked: root.tool = "select" }
            LingmoUI.IconButton { iconName: "edit-cut"; tipText: qsTr("Razor (B)"); checkable: true; checked: root.tool === "razor"; onClicked: root.tool = "razor" }
            LingmoUI.IconButton { iconName: "transform-move-horizontal"; tipText: qsTr("Roll edit point (R)"); checkable: true; checked: root.tool === "roll"; onClicked: root.tool = "roll" }
            LingmoUI.IconButton { iconName: "go-last-view"; tipText: qsTr("Slip contents (Y)"); checkable: true; checked: root.tool === "slip"; onClicked: root.tool = "slip" }
            LingmoUI.IconButton { iconName: "transform-shear-right"; tipText: qsTr("Slide between neighbours (U)"); checkable: true; checked: root.tool === "slide"; onClicked: root.tool = "slide" }
            ToolSeparator {}
            LingmoUI.IconButton { iconName: "format-justify-fill"; tipText: qsTr("Ripple: trims and deletes close gaps"); checkable: true; checked: root.rippleMode; onClicked: root.rippleMode = !root.rippleMode }
            LingmoUI.IconButton { iconName: "snap-nodes-cusp"; tipText: qsTr("Magnetic snapping (N)"); checkable: true; checked: root.snapping; onClicked: root.snapping = !root.snapping }
            ToolSeparator {}
            LingmoUI.IconButton { iconName: "edit-cut"; tipText: qsTr("Split at playhead (S)"); onClicked: editor.splitAtPlayhead() }
            LingmoUI.IconButton { iconName: "edit-delete"; tipText: qsTr("Ripple delete (Shift+Delete)"); enabled: editor.selection.length > 0; onClicked: editor.deleteSelected(true) }
            LingmoUI.IconButton { iconName: "kdenlive-composite"; tipText: qsTr("Cross dissolve at playhead (Ctrl+D)"); onClicked: editor.addTransitionAtPlayhead("dissolve", Math.round(editor.fps / 2)) }
            LingmoUI.IconButton { iconName: "media-playback-pause"; tipText: qsTr("Freeze frame (Shift+F)"); onClicked: editor.freezeFrame() }
            LingmoUI.IconButton { iconName: "bookmark-new"; tipText: qsTr("Add marker (M)"); onClicked: editor.addMarker(player.timelinePosition) }
            LingmoUI.IconButton { iconName: "list-add"; tipText: qsTr("Add track"); onClicked: { laneMenu.track = 0; laneMenu.frame = player.timelinePosition; laneMenu.popup() } }
            ToolSeparator {}
            LingmoUI.IconButton { iconName: "zoom-out"; tipText: qsTr("Zoom out (Ctrl+wheel)"); onClicked: panel.zoom(1 / 1.5, flick.width / 2) }
            Slider {
                Layout.preferredWidth: 110
                from: Math.log(0.02)
                to: Math.log(40)
                value: Math.log(panel.ppf)
                onMoved: panel.zoom(Math.exp(value) / panel.ppf, flick.width / 2)
            }
            LingmoUI.IconButton { iconName: "zoom-in"; tipText: qsTr("Zoom in"); onClicked: panel.zoom(1.5, flick.width / 2) }
            LingmoUI.IconButton { iconName: "zoom-fit-best"; tipText: qsTr("Fit timeline"); onClicked: { panel.userZoomed = false; panel.fit() } }
        }
    ]

    // ------------------------------------------------------------ body
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Track headers
        Item {
            Layout.preferredWidth: panel.headerWidth
            Layout.fillHeight: true
            clip: true

            Rectangle {
                id: corner
                width: parent.width
                height: ruler.height
                color: "transparent"
                Label {
                    anchors.centerIn: parent
                    text: editor.timecode(player.timelinePosition)
                    font.pointSize: LingmoUI.Units.fontBody
                    font.features: { "tnum": 1 }
                    color: LingmoUI.Theme.highlightColor
                }
            }
            Flickable {
                anchors.top: corner.bottom
                anchors.bottom: parent.bottom
                width: parent.width
                contentHeight: lanesColumn.height
                contentY: flick.contentY
                interactive: false
                Column {
                    width: parent.width
                    Repeater {
                        model: editor.timeline
                        delegate: TrackHeader {
                            width: panel.headerWidth
                            height: panel.trackHeight(model.kind)
                            trackIndex: index
                            compact: panel.compact
                            onContextMenuRequested: { laneMenu.track = index; laneMenu.frame = player.timelinePosition; laneMenu.popup() }
                        }
                    }
                }
            }
        }

        // Ruler + lanes
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Canvas {
                id: ruler
                width: parent.width
                height: 26
                property real offset: flick.contentX
                property real scale: panel.ppf
                property int playhead: player.timelinePosition
                onOffsetChanged: requestPaint()
                onScaleChanged: requestPaint()
                onWidthChanged: requestPaint()
                Connections {
                    target: LingmoUI.Theme
                    function onDarkModeChanged() { ruler.requestPaint() }
                }
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const fps = editor.fps
                    // choose a tick step (in frames) giving >= 70 px between labels
                    const steps = [1, 2, 5, 10, Math.round(fps / 2), Math.round(fps), 2 * fps, 5 * fps, 10 * fps, 30 * fps, 60 * fps, 300 * fps, 600 * fps, 1800 * fps, 3600 * fps]
                    let step = steps[steps.length - 1]
                    for (let i = 0; i < steps.length; ++i)
                        if (steps[i] * scale >= 70) { step = steps[i]; break }
                    const minor = step / 5
                    const first = Math.floor(offset / scale / minor) * minor
                    ctx.strokeStyle = LingmoUI.Theme.borderColor
                    ctx.fillStyle = LingmoUI.Theme.secondaryTextColor
                    ctx.font = Math.round(LingmoUI.Units.fontCaption * 1.25) + "px sans-serif"
                    ctx.lineWidth = 1
                    for (let f = first; f * scale - offset < width; f += minor) {
                        const x = Math.round(f * scale - offset) + 0.5
                        const major = Math.abs(f / step - Math.round(f / step)) < 1e-6
                        ctx.beginPath()
                        ctx.moveTo(x, height)
                        ctx.lineTo(x, major ? 8 : height - 6)
                        ctx.stroke()
                        if (major)
                            ctx.fillText(editor.timecode(Math.round(f)), x + 3, 12)
                    }
                    ctx.beginPath()
                    ctx.moveTo(0, height - 0.5)
                    ctx.lineTo(width, height - 0.5)
                    ctx.stroke()
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    function scrub(x) {
                        if (player.mode !== "timeline")
                            player.setMode("timeline")
                        player.seek(panel.frameOf(x + flick.contentX))
                    }
                    onPressed: function(m) { player.pause(); scrub(m.x) }
                    onPositionChanged: function(m) { if (pressed) scrub(m.x) }
                }
                // markers
                Repeater {
                    model: panel.model.markers
                    Rectangle {
                        x: panel.xOf(modelData.frame) - flick.contentX - 5
                        y: ruler.height - 12
                        width: 10
                        height: 10
                        radius: 2
                        rotation: 45
                        color: LingmoUI.Theme[modelData.color + "Color"] || LingmoUI.Theme.blueColor
                        ToolTip.visible: markerMouse.containsMouse
                        ToolTip.text: modelData.name
                        MouseArea {
                            id: markerMouse
                            anchors.fill: parent
                            anchors.margins: -3
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(m) {
                                if (m.button === Qt.RightButton)
                                    editor.removeMarker(index)
                                else
                                    player.seek(modelData.frame)
                            }
                            onDoubleClicked: { markerDialog.index = index; markerName.text = modelData.name; markerDialog.open() }
                        }
                    }
                }
                // in/out range of the timeline
                Rectangle {
                    visible: player.markIn >= 0 || player.markOut >= 0
                    x: panel.xOf(Math.max(0, player.markIn)) - flick.contentX
                    width: panel.xOf((player.markOut >= 0 ? player.markOut : panel.model.duration) - Math.max(0, player.markIn))
                    height: 4
                    color: LingmoUI.Theme.highlightColor
                    opacity: 0.7
                }
            }

            Flickable {
                id: flick
                anchors.top: ruler.bottom
                anchors.bottom: parent.bottom
                width: parent.width
                contentWidth: Math.max(width, panel.xOf(panel.model.duration + editor.fps * 30))
                contentHeight: Math.max(height, lanesColumn.height)
                boundsBehavior: Flickable.StopAtBounds
                interactive: false
                clip: true
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOn }
                ScrollBar.vertical: ScrollBar {}

                WheelHandler {
                    acceptedModifiers: Qt.ControlModifier
                    onWheel: function(event) { panel.zoom(event.angleDelta.y > 0 ? 1.25 : 0.8, event.x - flick.contentX) }
                }
                WheelHandler {
                    acceptedModifiers: Qt.NoModifier
                    onWheel: function(event) {
                        if (Math.abs(event.angleDelta.x) > Math.abs(event.angleDelta.y))
                            flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - event.angleDelta.x))
                        else
                            flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - event.angleDelta.y / 2))
                    }
                }
                WheelHandler {
                    acceptedModifiers: Qt.ShiftModifier
                    onWheel: function(event) { flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - event.angleDelta.y)) }
                }

                Column {
                    id: lanesColumn
                    width: flick.contentWidth

                    Repeater {
                        id: lanes
                        model: editor.timeline
                        delegate: Item {
                            id: lane
                            required property int index
                            required property var clips
                            required property string kind
                            required property bool locked
                            required property bool hidden
                            required property bool muted
                            width: lanesColumn.width
                            height: panel.trackHeight(kind)

                            Rectangle {
                                anchors.fill: parent
                                color: lane.index % 2 ? LingmoUI.Theme.backgroundColor : "transparent"
                                opacity: 0.6
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: LingmoUI.Theme.dividerColor
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onPressed: function(m) {
                                    if (m.button === Qt.RightButton) {
                                        laneMenu.track = lane.index
                                        laneMenu.frame = panel.frameOf(m.x)
                                        laneMenu.popup()
                                        return
                                    }
                                    editor.clearSelection()
                                    if (player.mode !== "timeline")
                                        player.setMode("timeline")
                                    player.seek(panel.frameOf(m.x))
                                }
                            }
                            DropArea {
                                anchors.fill: parent
                                keys: ["studio/media", "text/uri-list"]
                                onEntered: function(drag) { dropMarker.visible = true; dropMarker.x = drag.x }
                                onPositionChanged: function(drag) { dropMarker.x = panel.xOf(panel.snap(panel.frameOf(drag.x))) }
                                onExited: dropMarker.visible = false
                                onDropped: function(drop) {
                                    dropMarker.visible = false
                                    const frame = panel.snap(panel.frameOf(drop.x))
                                    if (drop.formats.indexOf("studio/media") >= 0)
                                        editor.addMediaToTimeline(drop.getDataAsString("studio/media"), lane.index, frame, -1, -1, root.rippleMode)
                                    else if (drop.hasUrls)
                                        editor.importUrls(drop.urls, lane.index, frame)
                                    drop.accept()
                                }
                            }
                            Rectangle {
                                id: dropMarker
                                visible: false
                                width: 2
                                height: parent.height
                                color: LingmoUI.Theme.highlightColor
                            }
                            Repeater {
                                model: lane.clips
                                delegate: ClipItem {
                                    owner: panel
                                    info: modelData
                                    trackIndex: lane.index
                                    trackKind: lane.kind
                                    locked: lane.locked
                                    dimmed: lane.hidden || (lane.kind === "audio" && lane.muted)
                                    height: lane.height - 3
                                    y: 1
                                }
                            }
                            Rectangle {
                                anchors.fill: parent
                                visible: lane.locked
                                color: LingmoUI.Theme.textColor
                                opacity: 0.05
                            }
                        }
                    }
                }

                // marker lines
                Repeater {
                    model: panel.model.markers
                    Rectangle {
                        x: panel.xOf(modelData.frame)
                        width: 1
                        height: lanesColumn.height
                        color: LingmoUI.Theme[modelData.color + "Color"] || LingmoUI.Theme.blueColor
                        opacity: 0.5
                    }
                }
            }

            // playhead across ruler and lanes
            Item {
                x: panel.xOf(player.timelinePosition) - flick.contentX
                visible: x >= -1 && x <= parent.width
                height: parent.height
                Rectangle {
                    x: -5
                    width: 11
                    height: 10
                    radius: 2
                    color: LingmoUI.Theme.redColor
                }
                Rectangle {
                    width: 1
                    height: parent.height
                    color: LingmoUI.Theme.redColor
                }
            }
        }
    }
}
