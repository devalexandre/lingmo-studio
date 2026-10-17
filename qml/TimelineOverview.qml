/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

import QtQuick

// The whole program in a thin strip: every clip as a coloured dash per
// track, the visible part of the timeline as a frame, and the playhead.
// Click or drag to move the view (`viewStart`/`viewFrames` in frames).
Item {
    id: strip

    property int viewStart: 0
    property int viewFrames: 0
    property bool showView: true
    readonly property int total: Math.max(editor.timeline.duration + Math.round(editor.fps * 5), Math.round(editor.fps * 10))
    signal scrubbed(int frame)      // centre of the view requested at frame

    implicitHeight: 18

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: StudioTheme.well
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 2
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const tl = editor.timeline
            const n = tl.trackCount
            if (n === 0)
                return
            const rowH = Math.max(1, height / n)
            const scale = width / strip.total
            for (let t = 0; t < n; ++t) {
                const kind = tl.trackKind(t)
                ctx.fillStyle = kind === "audio" ? StudioTheme.audioClipEdge : kind === "subtitle" ? StudioTheme.captionClipEdge : StudioTheme.videoClipEdge
                const clips = tl.clipsOf(t)
                for (let i = 0; i < clips.length; ++i) {
                    const c = clips[i]
                    ctx.fillRect(c.position * scale, t * rowH + 0.5, Math.max(1, c.duration * scale - 1), Math.max(1, rowH - 1))
                }
            }
        }
        Connections {
            target: editor.timeline
            function onChanged() { canvas.requestPaint() }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    // visible window of the timeline
    Rectangle {
        visible: strip.showView && strip.viewFrames > 0 && strip.viewFrames < strip.total
        x: 2 + strip.viewStart / strip.total * (strip.width - 4)
        width: Math.max(6, strip.viewFrames / strip.total * (strip.width - 4))
        height: parent.height
        radius: 3
        color: StudioTheme.alpha(StudioTheme.text, 0.06)
        border.width: 1
        border.color: StudioTheme.alpha(StudioTheme.text, 0.45)
    }
    // playhead
    Rectangle {
        x: 2 + editor.player.timelinePosition / strip.total * (strip.width - 4)
        width: 2
        height: parent.height
        color: StudioTheme.playhead
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function frameAt(x) { return Math.max(0, Math.round((x - 2) / Math.max(1, strip.width - 4) * strip.total)) }
        onPressed: function(m) { strip.scrubbed(frameAt(m.x)) }
        onPositionChanged: function(m) { if (pressed) strip.scrubbed(frameAt(m.x)) }
    }
}
