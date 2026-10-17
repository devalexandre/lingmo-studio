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

// Multi-track timeline: tool row, program overview, timecode ruler, track
// headers, clips, playhead and markers. Edits are previewed while dragging
// and committed to the editor on release.
//   mode "normal"   — the Edit page timeline
//   mode "overview" — the whole program, always fitted (Cut page, upper)
//   mode "zoomed"   — close-up that keeps the playhead centred (Cut page, lower)
StudioPanel {
    id: panel

    property real ppf: 2.0 // pixels per frame
    property bool showToolbar: true
    property bool showOverview: mode === "normal"
    property bool compact: false
    property string mode: "normal"
    property real zoomedPpf: 6
    readonly property bool overview: mode === "overview"
    readonly property int headerWidth: overview ? 44 : compact ? 128 : 168
    readonly property var model: editor.timeline
    readonly property var player: editor.player
    readonly property string tool: root.tool
    readonly property bool snapping: root.snapping
    // last snapped frame, shown as a magnet line while dragging
    property int snapFrame: -1

    showHeader: showToolbar
    color: StudioTheme.panel

    function trackHeight(kind) {
        if (overview)
            return kind === "subtitle" ? 16 : kind === "audio" ? 18 : 24
        if (kind === "subtitle")
            return compact ? 26 : 30
        if (kind === "audio")
            return compact ? 44 : 52
        return compact ? 46 : 66
    }
    function xOf(frame) { return frame * ppf }
    function frameOf(contentX) { return Math.max(0, Math.round(contentX / ppf)) }
    function fit() {
        if (mode === "zoomed") {
            ppf = zoomedPpf
            centreOnPlayhead()
            return
        }
        const frames = Math.max(model.duration, Math.round(editor.fps * 10))
        ppf = Math.max(0.02, (flick.width - (overview ? 8 : 60)) / frames)
        if (overview)
            flick.contentX = 0
    }
    function centreOnPlayhead() {
        flick.contentX = Math.max(0, panel.xOf(player.timelinePosition) - flick.width / 2)
    }
    property bool userZoomed: false
    function zoom(factor, anchorX) {
        if (overview)
            return
        userZoomed = true
        const frame = (flick.contentX + anchorX) / ppf
        ppf = Math.max(0.02, Math.min(40, ppf * factor))
        flick.contentX = Math.max(0, frame * ppf - anchorX)
    }
    function snap(frame, ignoreClip) {
        if (!snapping)
            return frame
        const s = editor.snapFrame(frame, Math.max(1, Math.round(10 / ppf)), ignoreClip || "")
        if (s !== frame) {
            snapFrame = s
            snapClear.restart()
        }
        return s
    }
    Timer { id: snapClear; interval: 450; onTriggered: panel.snapFrame = -1 }
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
    // the clip on another track that moves with this one (detached audio)
    function linkedPartner(info, track) {
        if (!info.linked)
            return ""
        for (let t = 0; t < model.trackCount; ++t) {
            if (t === track)
                continue
            const clips = model.clipsOf(t)
            for (let i = 0; i < clips.length; ++i)
                if (clips[i].linked && clips[i].mediaId === info.mediaId && clips[i].position === info.position)
                    return clips[i].id
        }
        return ""
    }

    Component.onCompleted: Qt.callLater(fit)
    // hidden pages get their real size later: keep fitting until the user zooms
    Connections {
        target: flick
        function onWidthChanged() { if ((!panel.userZoomed || panel.overview) && flick.width > 100) Qt.callLater(panel.fit) }
    }
    Connections {
        target: panel.model
        function onChanged() {
            if (panel.overview || (!panel.userZoomed && panel.mode === "normal" && panel.xOf(panel.model.duration) > flick.width))
                Qt.callLater(panel.fit)
        }
    }
    Connections {
        target: editor
        function onProjectChanged() { panel.userZoomed = false; Qt.callLater(panel.fit) }
        function onSequenceChanged() { Qt.callLater(panel.fit) }
    }
    // keep the playhead in view while playing (centred in the close-up)
    Connections {
        target: player
        function onPositionChanged() {
            if (panel.mode === "zoomed") {
                panel.centreOnPlayhead()
                return
            }
            if (panel.overview || !player.playing || player.mode !== "timeline")
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
            text: qsTr("Automatic captions…")
            enabled: clipMenu.target.hasAudio === true && clipMenu.target.kind === "media"
            onTriggered: root.openCaptions(clipMenu.target.id)
        }
        MenuItem {
            text: qsTr("Generate cuts for Shorts/Reels…")
            enabled: clipMenu.target.kind === "media" && clipMenu.target.mediaKind === "video"
            onTriggered: root.openShorts(clipMenu.target.mediaId)
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

    // ------------------------------------------------------------ tool row
    leading: [
        Row {
            spacing: 1
            Layout.leftMargin: -6
            StudioButton { iconName: "tool-select"; tip: qsTr("Selection mode"); shortcut: "A"; checkable: true; checked: root.tool === "select"; onClicked: root.tool = "select" }
            StudioButton { iconName: "tool-trim"; tip: qsTr("Trim edit mode: drag a cut to roll it"); shortcut: "T"; checkable: true; checked: root.tool === "roll"; onClicked: root.tool = "roll" }
            StudioButton { iconName: "tool-slip"; tip: qsTr("Slip contents"); shortcut: "Y"; checkable: true; checked: root.tool === "slip"; onClicked: root.tool = "slip" }
            StudioButton { iconName: "tool-slide"; tip: qsTr("Slide between neighbours"); shortcut: "U"; checkable: true; checked: root.tool === "slide"; onClicked: root.tool = "slide" }
            StudioButton { iconName: "tool-blade"; tip: qsTr("Blade edit mode"); shortcut: "B"; checkable: true; checked: root.tool === "razor"; onClicked: root.tool = "razor" }
        },
        Rectangle { width: 1; height: 16; color: StudioTheme.border },
        Row {
            spacing: 1
            StudioButton { iconName: "snap"; tip: qsTr("Magnetic snapping"); shortcut: "N"; checkable: true; checked: root.snapping; checkedColor: StudioTheme.snap; onClicked: root.snapping = !root.snapping }
            StudioButton { iconName: "link"; tip: qsTr("Linked selection"); shortcut: "Ctrl+Shift+L"; checkable: true; checked: root.linkedSelection; onClicked: root.linkedSelection = !root.linkedSelection }
            StudioButton { iconName: "ripple"; tip: qsTr("Ripple: trims and deletes close gaps"); checkable: true; checked: root.rippleMode; onClicked: root.rippleMode = !root.rippleMode }
        },
        Rectangle { width: 1; height: 16; color: StudioTheme.border; visible: panel.width > 900 },
        Row {
            spacing: 1
            visible: panel.width > 900
            StudioButton { iconName: "split"; tip: qsTr("Split at playhead"); shortcut: "S"; onClicked: editor.splitAtPlayhead() }
            StudioButton { iconName: "trash"; tip: qsTr("Ripple delete"); shortcut: "Shift+Delete"; enabled: editor.selection.length > 0; onClicked: editor.deleteSelected(true) }
            StudioButton { iconName: "dissolve"; tip: qsTr("Cross dissolve at playhead"); shortcut: "Ctrl+D"; onClicked: editor.addTransitionAtPlayhead("dissolve", Math.round(editor.fps / 2)) }
            StudioButton { iconName: "freeze"; tip: qsTr("Freeze frame"); shortcut: "Shift+F"; onClicked: editor.freezeFrame() }
            StudioButton { iconName: "marker"; tip: qsTr("Add marker"); shortcut: "M"; onClicked: editor.addMarker(player.timelinePosition) }
            StudioButton { iconName: "add-track"; tip: qsTr("Add track"); onClicked: { laneMenu.track = 0; laneMenu.frame = player.timelinePosition; laneMenu.popup() } }
        },
        Row {
            spacing: 2
            Repeater {
                model: editor.sequencePath.length > 1 ? editor.sequencePath : []
                StudioButton {
                    text: modelData
                    size: 22
                    fontSize: StudioTheme.fontSmall
                    enabled: index < editor.sequencePath.length - 1
                    onClicked: editor.leaveCompound(index)
                }
            }
        }
    ]
    actions: [
        StudioButton { iconName: "zoom-out"; tip: qsTr("Zoom out"); shortcut: qsTr("Ctrl+wheel"); onClicked: panel.zoom(1 / 1.5, flick.width / 2) },
        Slider {
            Layout.preferredWidth: 110
            from: Math.log(0.02)
            to: Math.log(40)
            value: Math.log(panel.ppf)
            onMoved: panel.zoom(Math.exp(value) / panel.ppf, flick.width / 2)
        },
        StudioButton { iconName: "zoom-in"; tip: qsTr("Zoom in"); onClicked: panel.zoom(1.5, flick.width / 2) },
        StudioButton { iconName: "zoom-fit"; tip: qsTr("Fit timeline"); shortcut: "Shift+Z"; onClicked: { panel.userZoomed = false; panel.fit() } }
    ]
    Shortcut { sequence: "Shift+Z"; enabled: panel.visible && panel.mode === "normal"; onActivated: { panel.userZoomed = false; panel.fit() } }

    // ------------------------------------------------------------ body
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // program overview
        Item {
            visible: panel.showOverview
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            TimelineOverview {
                anchors.fill: parent
                anchors.leftMargin: panel.headerWidth + 4
                anchors.rightMargin: 6
                anchors.topMargin: 4
                anchors.bottomMargin: 2
                viewStart: panel.frameOf(flick.contentX)
                viewFrames: Math.round(flick.width / panel.ppf)
                onScrubbed: function(frame) {
                    flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, panel.xOf(frame) - flick.width / 2))
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                    color: StudioTheme.panel
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        visible: !panel.overview
                        text: editor.timecode(player.timelinePosition)
                        font.family: StudioTheme.font
                        font.pixelSize: panel.compact ? StudioTheme.fontBody : StudioTheme.fontLarge
                        font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                        color: StudioTheme.playhead
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: StudioTheme.divider }
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
                                height: panel.trackHeight(kind)
                                trackIndex: index
                                compact: panel.compact
                                minimal: panel.overview
                                onContextMenuRequested: { laneMenu.track = index; laneMenu.frame = player.timelinePosition; laneMenu.popup() }
                            }
                        }
                    }
                }
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: StudioTheme.divider }
            }

            // Ruler + lanes
            Item {
                id: lanesArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Canvas {
                    id: ruler
                    width: parent.width
                    height: panel.overview ? 16 : 26
                    property real offset: flick.contentX
                    property real scale: panel.ppf
                    onOffsetChanged: requestPaint()
                    onScaleChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = StudioTheme.panel
                        ctx.fillRect(0, 0, width, height)
                        const fps = editor.fps
                        // a tick step (in frames) giving >= 80 px between labels
                        const steps = [1, 2, 5, 10, Math.round(fps / 2), Math.round(fps), 2 * fps, 5 * fps, 10 * fps, 30 * fps, 60 * fps, 300 * fps, 600 * fps, 1800 * fps, 3600 * fps]
                        let step = steps[steps.length - 1]
                        for (let i = 0; i < steps.length; ++i)
                            if (steps[i] * scale >= 80) { step = steps[i]; break }
                        const minor = step / 5
                        const first = Math.floor(offset / scale / minor) * minor
                        ctx.lineWidth = 1
                        ctx.font = "10px Inter, sans-serif"
                        for (let f = first; f * scale - offset < width; f += minor) {
                            const x = Math.round(f * scale - offset) + 0.5
                            const major = Math.abs(f / step - Math.round(f / step)) < 1e-6
                            ctx.strokeStyle = major ? "#4A4D55" : "#33353B"
                            ctx.beginPath()
                            ctx.moveTo(x, height)
                            ctx.lineTo(x, major ? height - 10 : height - 5)
                            ctx.stroke()
                            if (major && !panel.overview) {
                                ctx.fillStyle = StudioTheme.textSecondary
                                ctx.fillText(editor.timecode(Math.round(f)), x + 4, 11)
                            }
                        }
                        ctx.strokeStyle = StudioTheme.divider
                        ctx.beginPath()
                        ctx.moveTo(0, height - 0.5)
                        ctx.lineTo(width, height - 0.5)
                        ctx.stroke()
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor
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
                            color: ({ blue: StudioTheme.trackVideo, green: StudioTheme.trackAudio, orange: StudioTheme.warning, red: StudioTheme.danger,
                                      yellow: StudioTheme.solo, purple: StudioTheme.titleClipEdge, pink: "#D86FA8" })[modelData.color] || StudioTheme.trackVideo
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
                        height: 3
                        color: StudioTheme.accent
                        opacity: 0.85
                    }
                }

                Flickable {
                    id: flick
                    anchors.top: ruler.bottom
                    anchors.bottom: parent.bottom
                    width: parent.width
                    contentWidth: Math.max(width, panel.xOf(panel.model.duration + (panel.overview ? 0 : editor.fps * 30)))
                    contentHeight: Math.max(height, lanesColumn.height)
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    clip: true
                    ScrollBar.horizontal: ScrollBar { policy: panel.mode === "normal" ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                    ScrollBar.vertical: ScrollBar { policy: panel.overview ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded }

                    WheelHandler {
                        acceptedModifiers: Qt.ControlModifier
                        onWheel: function(event) { panel.zoom(event.angleDelta.y > 0 ? 1.25 : 0.8, event.x - flick.contentX) }
                    }
                    WheelHandler {
                        acceptedModifiers: Qt.NoModifier
                        onWheel: function(event) {
                            if (panel.mode === "zoomed") {
                                player.seek(Math.max(0, player.timelinePosition - Math.round(event.angleDelta.y / 40)))
                                return
                            }
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
                                    color: lane.index % 2 ? StudioTheme.laneOdd : StudioTheme.laneEven
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: StudioTheme.divider
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
                                    color: StudioTheme.accent
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
                                        minimal: panel.overview
                                        height: lane.height - 3
                                        y: 1
                                    }
                                }
                                // locked tracks are hatched
                                Canvas {
                                    anchors.fill: parent
                                    visible: lane.locked
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.reset()
                                        ctx.strokeStyle = "rgba(255,255,255,0.05)"
                                        ctx.lineWidth = 1
                                        for (let x = -height; x < width; x += 8) {
                                            ctx.beginPath(); ctx.moveTo(x, height); ctx.lineTo(x + height, 0); ctx.stroke()
                                        }
                                    }
                                    onWidthChanged: if (visible) requestPaint()
                                    onVisibleChanged: if (visible) requestPaint()
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
                            color: StudioTheme.textSecondary
                            opacity: 0.25
                        }
                    }
                }

                // magnetic snapping indicator
                Item {
                    visible: panel.snapFrame >= 0
                    x: panel.xOf(panel.snapFrame) - flick.contentX
                    height: parent.height
                    Rectangle {
                        y: ruler.height
                        width: 1
                        height: parent.height - ruler.height
                        color: StudioTheme.snap
                    }
                    Rectangle {
                        x: -9
                        y: 2
                        width: 18
                        height: 14
                        radius: 3
                        color: StudioTheme.snap
                        StudioIcon { anchors.centerIn: parent; name: "snap"; size: 11; color: "#16171A" }
                    }
                }

                // playhead across ruler and lanes, with a timecode flag
                Item {
                    id: playhead
                    x: panel.xOf(player.timelinePosition) - flick.contentX
                    visible: x >= -1 && x <= parent.width
                    height: parent.height
                    Rectangle {
                        width: 1
                        height: parent.height
                        color: StudioTheme.playhead
                    }
                    Rectangle {
                        visible: !panel.overview
                        x: -1
                        y: 0
                        width: flagText.implicitWidth + 10
                        height: 15
                        radius: 3
                        color: StudioTheme.playhead
                        Text {
                            id: flagText
                            anchors.centerIn: parent
                            text: editor.timecode(player.timelinePosition)
                            color: "#FFFFFF"
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontTiny
                            font.weight: Font.DemiBold
                            font.features: { "tnum": 1 }
                        }
                    }
                    Canvas {
                        visible: panel.overview
                        x: -5
                        width: 11
                        height: 7
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = StudioTheme.playhead
                            ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(11, 0); ctx.lineTo(5.5, 7); ctx.closePath(); ctx.fill()
                        }
                    }
                }
            }
        }
    }
}
