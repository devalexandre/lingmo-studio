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

// Pan knob (-1 left … +1 right): drag up/down or use the wheel;
// double-click centres it.
Item {
    id: knob

    property real value: 0
    property int size: 38
    signal moved(real value)
    signal reset()

    implicitWidth: size
    implicitHeight: size

    readonly property real angle: value * 135   // degrees from the top

    Canvas {
        id: canvas
        anchors.fill: parent
        property real a: knob.angle
        property bool hot: mouse.containsMouse || mouse.pressed
        onAChanged: requestPaint()
        onHotChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const cx = width / 2, cy = height / 2, r = Math.min(cx, cy) - 3
            const start = (-90 - 135) * Math.PI / 180, end = (-90 + 135) * Math.PI / 180
            // track
            ctx.lineWidth = 3
            ctx.lineCap = "round"
            ctx.strokeStyle = "#33363D"
            ctx.beginPath(); ctx.arc(cx, cy, r, start, end); ctx.stroke()
            // value arc from the centre
            const top = -Math.PI / 2, val = (a - 90) * Math.PI / 180
            ctx.strokeStyle = StudioTheme.accent
            ctx.beginPath(); ctx.arc(cx, cy, r, Math.min(top, val), Math.max(top, val)); ctx.stroke()
            // body
            ctx.fillStyle = hot ? "#34373E" : "#2A2C32"
            ctx.beginPath(); ctx.arc(cx, cy, r - 5, 0, 2 * Math.PI); ctx.fill()
            // pointer
            ctx.strokeStyle = StudioTheme.text
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(cx + Math.cos(val) * (r - 12), cy + Math.sin(val) * (r - 12))
            ctx.lineTo(cx + Math.cos(val) * (r - 6), cy + Math.sin(val) * (r - 6))
            ctx.stroke()
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        property real startY: 0
        property real startValue: 0
        onPressed: function(m) { startY = m.y; startValue = knob.value }
        onPositionChanged: function(m) {
            if (!pressed)
                return
            knob.moved(Math.max(-1, Math.min(1, startValue + (startY - m.y) / 80)))
        }
        onDoubleClicked: knob.reset()
        onWheel: function(w) { knob.moved(Math.max(-1, Math.min(1, knob.value + (w.angleDelta.y > 0 ? 0.05 : -0.05)))) }
    }
    ToolTip.visible: mouse.containsMouse || mouse.pressed
    ToolTip.text: Math.abs(value) < 0.04 ? qsTr("Center") : (value < 0 ? qsTr("L %1").arg(Math.round(-value * 100)) : qsTr("R %1").arg(Math.round(value * 100)))
}
