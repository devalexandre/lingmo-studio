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

// Tone curve editor ("x/y x/y ..." strings, as ffmpeg's curves filter takes).
// Click adds a point, drag moves it, right-click removes it.
ColumnLayout {
    id: curves

    property string channel: "master"
    property string points: "0/0 1/1"
    signal edited(string points)

    function parse(s) {
        const out = []
        String(s).trim().split(/\s+/).forEach(function(p) {
            const xy = p.split("/")
            if (xy.length === 2)
                out.push({ x: Number(xy[0]), y: Number(xy[1]) })
        })
        out.sort(function(a, b) { return a.x - b.x })
        return out.length >= 2 ? out : [{ x: 0, y: 0 }, { x: 1, y: 1 }]
    }
    function serialize(list) {
        return list.map(function(p) { return p.x.toFixed(3) + "/" + p.y.toFixed(3) }).join(" ")
    }
    property var list: parse(points)
    onPointsChanged: { list = parse(points); canvas.requestPaint() }

    readonly property color curveColor: channel === "red" ? LingmoUI.Theme.redColor : channel === "green" ? LingmoUI.Theme.greenColor
                                       : channel === "blue" ? LingmoUI.Theme.blueColor : LingmoUI.Theme.textColor

    Canvas {
        id: canvas
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 120
        property int dragging: -1
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = LingmoUI.Theme.alternateBackgroundColor
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = LingmoUI.Theme.borderColor
            for (let i = 1; i < 4; ++i) {
                ctx.beginPath(); ctx.moveTo(width * i / 4, 0); ctx.lineTo(width * i / 4, height); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(0, height * i / 4); ctx.lineTo(width, height * i / 4); ctx.stroke()
            }
            ctx.beginPath(); ctx.moveTo(0, height); ctx.lineTo(width, 0); ctx.stroke()
            // smooth curve (Catmull-Rom through the points)
            const p = curves.list
            ctx.strokeStyle = curves.curveColor
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let i = 0; i < p.length - 1; ++i) {
                const p0 = p[Math.max(0, i - 1)], p1 = p[i], p2 = p[i + 1], p3 = p[Math.min(p.length - 1, i + 2)]
                for (let t = 0; t <= 1.0001; t += 0.05) {
                    const t2 = t * t, t3 = t2 * t
                    const x = 0.5 * (2 * p1.x + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3)
                    let y = 0.5 * (2 * p1.y + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)
                    y = Math.max(0, Math.min(1, y))
                    if (i === 0 && t === 0) ctx.moveTo(x * width, (1 - y) * height)
                    else ctx.lineTo(x * width, (1 - y) * height)
                }
            }
            ctx.stroke()
            ctx.fillStyle = curves.curveColor
            for (let i = 0; i < p.length; ++i) {
                ctx.beginPath()
                ctx.arc(p[i].x * width, (1 - p[i].y) * height, 4, 0, 2 * Math.PI)
                ctx.fill()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            function nearest(m) {
                for (let i = 0; i < curves.list.length; ++i) {
                    const dx = curves.list[i].x * width - m.x, dy = (1 - curves.list[i].y) * height - m.y
                    if (dx * dx + dy * dy < 100)
                        return i
                }
                return -1
            }
            onPressed: function(m) {
                const i = nearest(m)
                if (m.button === Qt.RightButton) {
                    if (i > 0 && i < curves.list.length - 1) {
                        const l = curves.list.slice()
                        l.splice(i, 1)
                        curves.edited(curves.serialize(l))
                    }
                    return
                }
                if (i >= 0) {
                    canvas.dragging = i
                } else {
                    const l = curves.list.slice()
                    l.push({ x: m.x / width, y: 1 - m.y / height })
                    l.sort(function(a, b) { return a.x - b.x })
                    curves.list = l
                    canvas.dragging = l.findIndex(function(p) { return Math.abs(p.x - m.x / width) < 1e-6 })
                    canvas.requestPaint()
                }
            }
            onPositionChanged: function(m) {
                if (canvas.dragging < 0)
                    return
                const l = curves.list.slice()
                const i = canvas.dragging
                const minX = i === 0 ? 0 : l[i - 1].x + 0.01
                const maxX = i === l.length - 1 ? 1 : l[i + 1].x - 0.01
                l[i] = { x: (i === 0 ? 0 : (i === l.length - 1 ? 1 : Math.max(minX, Math.min(maxX, m.x / width)))),
                         y: Math.max(0, Math.min(1, 1 - m.y / height)) }
                curves.list = l
                canvas.requestPaint()
            }
            onReleased: {
                if (canvas.dragging >= 0)
                    curves.edited(curves.serialize(curves.list))
                canvas.dragging = -1
            }
            onDoubleClicked: curves.edited("0/0 1/1")
        }
    }
}
