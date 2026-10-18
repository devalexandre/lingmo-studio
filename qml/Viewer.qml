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
import Lingmo.Studio 1.0

// One viewer: timecode above the picture, a scrubber and the transport
// below. There is a single MLT player, so a viewer bound to a `role`
// ("source" or "timeline") shows live video while the player is in that
// mode and holds its last frame otherwise; clicking it takes the player
// over. "follow" shows whatever the player plays (one-viewer pages).
StudioPanel {
    id: viewer

    property string role: "follow"
    property bool compare: false
    property bool showEditButtons: false   // insert/overwrite/append (source)
    property bool showModeSwitch: false    // Fonte / Linha do tempo chips (follow)
    property bool showMeter: role !== "source"
    property bool dual: false              // part of a source|timeline pair
    property string label: ""

    readonly property var player: editor.player
    readonly property QtObject studioEditor: editor
    readonly property bool live: role === "follow" || player.mode === role
    readonly property bool sourceSide: role === "source" || (role === "follow" && player.mode === "source")
    readonly property bool hasSource: player.sourceMediaId.length > 0

    // remembered while the other viewer has the player
    property int heldSourcePos: 0
    property int heldSourceLen: 1
    property string heldSourceName: ""
    property var still: null
    property real lastGrab: 0

    readonly property int position: live ? player.position : (role === "source" ? heldSourcePos : player.timelinePosition)
    readonly property int duration: live ? player.duration : (role === "source" ? heldSourceLen : Math.max(1, editor.timeline.duration))
    readonly property int markIn: sourceSide ? player.sourceIn : player.markIn
    readonly property int markOut: sourceSide ? player.sourceOut : player.markOut

    function activate() {
        if (role === "follow" || player.mode === role)
            return true
        if (role === "source" && !hasSource)
            return false
        player.setMode(role)
        if (role === "source")
            player.seek(heldSourcePos)
        return true
    }
    function sourceName() {
        if (!hasSource)
            return ""
        for (let i = 0; i < editor.mediaBin.count; ++i)
            if (editor.mediaBin.idAt(i) === player.sourceMediaId)
                return editor.mediaBin.data(editor.mediaBin.index(i, 0), 258) || ""
        return ""
    }

    Connections {
        target: viewer.player
        function onPositionChanged() {
            if (viewer.player.mode === "source") {
                viewer.heldSourcePos = viewer.player.position
                viewer.heldSourceLen = viewer.player.duration
            }
        }
        function onModeChanged() {
            if (viewer.player.mode === "source") {
                viewer.heldSourceLen = viewer.player.duration
                viewer.heldSourceName = viewer.sourceName()
            }
        }
        function onFrameReady() {
            if (viewer.role !== "timeline" || viewer.player.mode !== "timeline")
                return
            // paused: shortly after the last frame; playing: about once a second
            if (!viewer.player.playing)
                grabTimer.restart()
            else if (!grabTimer.running && Date.now() - viewer.lastGrab > 1000)
                grabTimer.start()
        }
    }
    // the timeline viewer keeps a still of its last frame for when the
    // source viewer takes the player
    Timer {
        id: grabTimer
        interval: 220
        onTriggered: {
            if (viewer.role === "timeline" && viewer.player.mode === "timeline" && video.visible && video.width > 0)
                video.grabToImage(function(result) { viewer.still = result; viewer.lastGrab = Date.now() })
        }
    }

    title: label.length > 0 ? label
         : showModeSwitch ? ""
         : sourceSide ? qsTr("Source") : (editor.sequencePath.length > 1 ? editor.sequencePath[editor.sequencePath.length - 1] : qsTr("Timeline"))
    subtitle: sourceSide ? (live ? sourceName() : heldSourceName) : editor.projectName

    leading: [
        Row {
            visible: viewer.showModeSwitch
            spacing: 2
            Layout.leftMargin: 6
            Repeater {
                model: [{ mode: "source", text: qsTr("Source") }, { mode: "timeline", text: qsTr("Timeline") }]
                StudioButton {
                    text: modelData.text
                    size: 22
                    fontSize: StudioTheme.fontSmall
                    checkable: true
                    checked: viewer.player.mode === modelData.mode
                    enabled: modelData.mode === "timeline" || viewer.hasSource
                    onClicked: viewer.player.setMode(modelData.mode)
                }
            }
        }
    ]
    actions: [
        Text {
            text: editor.timecode(viewer.position)
            color: viewer.live ? StudioTheme.text : StudioTheme.textSecondary
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontTitle
            font.weight: Font.DemiBold
            font.features: { "tnum": 1 }
            Layout.rightMargin: 4
        },
        StudioButton {
            visible: viewer.role !== "source"
            text: viewer.player.previewHeight + "p"
            size: 22
            fontSize: StudioTheme.fontSmall
            tip: qsTr("Preview resolution")
            onClicked: resMenu.popup(this, 0, height)
            Menu {
                id: resMenu
                Repeater {
                    model: [1080, 720, 540, 360]
                    MenuItem {
                        text: modelData + "p"
                        checkable: true
                        checked: viewer.player.previewHeight === modelData
                        onTriggered: viewer.player.previewHeight = modelData
                    }
                }
            }
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        // ---------------------------------------------------- picture
        Rectangle {
            id: picture
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: StudioTheme.smallRadius
            color: StudioTheme.well
            clip: true
            border.width: viewer.dual && viewer.live ? 1 : 0
            border.color: StudioTheme.accent

            VideoView {
                id: video
                anchors.fill: parent
                anchors.margins: picture.border.width
                visible: viewer.live
                editor: viewer.studioEditor
                compare: viewer.compare && !viewer.sourceSide
                split: splitHandle.x / Math.max(1, width)
            }
            // the source's captions, when its speech is known (libass, like the export)
            CaptionOverlay {
                readonly property rect area: video.videoRect
                readonly property real aspect: viewer.studioEditor.sourceAspect
                readonly property bool wide: area.height > 0 && area.width / area.height > aspect
                visible: viewer.live && viewer.sourceSide && viewer.studioEditor.sourceCaptions !== "" && aspect > 0 && area.width > 0
                width: wide ? area.height * aspect : area.width
                height: wide ? area.height : area.width / Math.max(0.01, aspect)
                x: video.x + area.x + (area.width - width) / 2
                y: video.y + area.y + (area.height - height) / 2
                source: viewer.studioEditor.sourceCaptions
                time: viewer.player.position * 1000 / viewer.studioEditor.fps
            }
            // held frame of the inactive side
            Image {
                anchors.fill: parent
                visible: !viewer.live
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                source: viewer.live ? ""
                      : viewer.role === "source" ? (viewer.hasSource ? "image://thumb/" + viewer.player.sourceMediaId + "/" + viewer.heldSourcePos : "")
                      : (viewer.still ? viewer.still.url : "")
                opacity: 0.55
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: viewer.role === "source" ? !viewer.hasSource
                       : (editor.timeline.duration <= 1 && !viewer.sourceSide)
                text: viewer.role === "source" ? qsTr("Click a clip in the media pool to view it here")
                                               : qsTr("Drop clips on the timeline to see them here")
                color: StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontBody
            }
            MouseArea {
                anchors.fill: parent
                visible: !viewer.live
                cursorShape: Qt.PointingHandCursor
                onClicked: viewer.activate()
                ToolTip.visible: containsMouse && viewer.hasSource
                ToolTip.text: qsTr("Click to play in this viewer")
                hoverEnabled: true
            }
            // before/after divider (Color page)
            Rectangle {
                id: splitHandle
                visible: video.compare
                x: video.videoRect.x + video.videoRect.width / 2
                y: video.videoRect.y
                width: 2
                height: video.videoRect.height
                color: StudioTheme.accent
                Text {
                    anchors.right: parent.left
                    anchors.rightMargin: 6
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    text: qsTr("Before")
                    color: "#FFFFFF"
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                }
                Text {
                    anchors.left: parent.right
                    anchors.leftMargin: 6
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    text: qsTr("After")
                    color: "#FFFFFF"
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    cursorShape: Qt.SplitHCursor
                    drag.target: splitHandle
                    drag.axis: Drag.XAxis
                    drag.minimumX: video.videoRect.x
                    drag.maximumX: video.videoRect.x + video.videoRect.width
                }
            }
            LevelMeter {
                visible: viewer.showMeter && viewer.live
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: 6
                width: 8
                levelLeft: viewer.player.levelLeft
                levelRight: viewer.player.levelRight
            }
            // speed while shuttling
            Rectangle {
                visible: viewer.live && Math.abs(viewer.player.speed) > 1
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 8
                width: speedText.implicitWidth + 12
                height: 20
                radius: 4
                color: StudioTheme.alpha(StudioTheme.window, 0.8)
                Text {
                    id: speedText
                    anchors.centerIn: parent
                    text: viewer.player.speed + "×"
                    color: StudioTheme.accentText
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontSmall
                    font.weight: Font.DemiBold
                }
            }
        }

        // ---------------------------------------------------- scrubber
        Item {
            id: scrub
            Layout.fillWidth: true
            Layout.preferredHeight: 16
            readonly property real span: Math.max(1, viewer.duration - 1)
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 4
                radius: 2
                color: StudioTheme.raised
            }
            Rectangle {
                visible: viewer.markIn >= 0 || viewer.markOut >= 0
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(0, viewer.markIn) / scrub.span * parent.width
                width: Math.max(2, ((viewer.markOut >= 0 ? viewer.markOut : viewer.duration) - Math.max(0, viewer.markIn)) / scrub.span * parent.width)
                height: 4
                radius: 2
                color: StudioTheme.accent
                opacity: 0.8
            }
            Rectangle {
                x: Math.min(parent.width - width, viewer.position / scrub.span * parent.width - width / 2)
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 14
                radius: 1.5
                color: viewer.live ? StudioTheme.playhead : StudioTheme.textSecondary
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                function seekTo(x) { viewer.player.seek(Math.round(Math.max(0, Math.min(1, x / width)) * (viewer.player.duration - 1))) }
                onPressed: function(m) {
                    if (!viewer.activate())
                        return
                    viewer.player.pause()
                    seekTo(m.x)
                }
                onPositionChanged: function(m) { if (pressed && viewer.live) seekTo(m.x) }
            }
        }

        // ---------------------------------------------------- transport
        RowLayout {
            Layout.fillWidth: true
            spacing: 1

            StudioButton { iconName: "mark-in"; tip: qsTr("Mark in"); shortcut: "I"; size: 26; onClicked: if (viewer.activate()) viewer.player.markInHere() }
            StudioButton { iconName: "mark-out"; tip: qsTr("Mark out"); shortcut: "O"; size: 26; onClicked: if (viewer.activate()) viewer.player.markOutHere() }
            StudioButton {
                iconName: "clear-marks"; tip: qsTr("Clear in/out"); size: 26
                visible: viewer.width > 520
                onClicked: if (viewer.activate()) viewer.player.clearMarks()
            }
            Item { Layout.fillWidth: true }
            StudioButton { iconName: "go-start"; tip: qsTr("Start"); shortcut: "Home"; size: 26; visible: viewer.width > 380; onClicked: if (viewer.activate()) viewer.player.seek(0) }
            StudioButton {
                iconName: "edit-prev"; tip: qsTr("Previous edit"); shortcut: "Up"; size: 26
                visible: !viewer.sourceSide && viewer.width > 440
                onClicked: if (viewer.activate()) viewer.player.seek(editor.nextEdit(viewer.player.position, false))
            }
            StudioButton { iconName: "frame-prev"; tip: qsTr("Previous frame"); shortcut: "Left"; size: 26; onClicked: if (viewer.activate()) viewer.player.step(-1) }
            StudioButton {
                iconName: "play-reverse"; tip: qsTr("Play backwards"); shortcut: "J"; size: 26
                checkable: true
                checked: viewer.live && viewer.player.speed < 0
                onClicked: if (viewer.activate()) viewer.player.shuttle(-1)
            }
            StudioButton {
                iconName: viewer.live && viewer.player.playing ? "pause" : "play"
                tip: viewer.live && viewer.player.playing ? qsTr("Pause") : qsTr("Play")
                shortcut: qsTr("Space")
                size: 32
                iconSize: 18
                flat: false
                onClicked: if (viewer.activate()) viewer.player.togglePlay()
            }
            StudioButton {
                iconName: "fast-forward"; tip: qsTr("Faster forward"); shortcut: "L"; size: 26
                checkable: true
                checked: viewer.live && viewer.player.speed > 1
                onClicked: if (viewer.activate()) viewer.player.shuttle(1)
            }
            StudioButton { iconName: "frame-next"; tip: qsTr("Next frame"); shortcut: "Right"; size: 26; onClicked: if (viewer.activate()) viewer.player.step(1) }
            StudioButton {
                iconName: "edit-next"; tip: qsTr("Next edit"); shortcut: "Down"; size: 26
                visible: !viewer.sourceSide && viewer.width > 440
                onClicked: if (viewer.activate()) viewer.player.seek(editor.nextEdit(viewer.player.position, true))
            }
            StudioButton { iconName: "go-end"; tip: qsTr("End"); shortcut: "End"; size: 26; visible: viewer.width > 380; onClicked: if (viewer.activate()) viewer.player.seek(viewer.player.duration - 1) }
            Item { Layout.fillWidth: true }

            // three-point edits from the source
            StudioButton {
                visible: viewer.showEditButtons && viewer.sourceSide
                iconName: "insert"; tip: qsTr("Insert the marked range at the playhead, pushing clips right"); shortcut: "F9"; size: 26
                enabled: viewer.hasSource
                onClicked: root.editFromSource("insert")
            }
            StudioButton {
                visible: viewer.showEditButtons && viewer.sourceSide
                iconName: "overwrite"; tip: qsTr("Place the marked range at the playhead, replacing what is there"); shortcut: "F10"; size: 26
                enabled: viewer.hasSource
                onClicked: root.editFromSource("overwrite")
            }
            StudioButton {
                visible: viewer.showEditButtons && viewer.sourceSide
                iconName: "append"; tip: qsTr("Append to the end of the timeline"); shortcut: "Shift+F12"; size: 26
                enabled: viewer.hasSource
                onClicked: root.editFromSource("append")
            }
            Slider {
                visible: !viewer.sourceSide && viewer.width > 460
                Layout.preferredWidth: 64
                from: 0
                to: 1
                value: viewer.player.volume
                onMoved: viewer.player.volume = value
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Preview volume")
            }
        }
    }
}
