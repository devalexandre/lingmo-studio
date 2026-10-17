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

// Lift / gamma / gain wheel. The puck pushes the balance towards a hue; the
// slider below moves all three channels (master). Double-click resets.
ColumnLayout {
    id: wheel

    property string label: ""
    property real neutral: 0     // 0 for lift, 1 for gamma and gain
    property real range: 0.25    // largest per-channel offset at the rim
    property real r: neutral
    property real g: neutral
    property real b: neutral
    signal moved(real r, real g, real b)

    spacing: 4

    readonly property real master: (r + g + b) / 3 - neutral
    // chroma offsets -> 2D (x right = red, rotating through yellow, green...)
    readonly property real dx: ((r - neutral) - ((g - neutral) + (b - neutral)) / 2) / range
    readonly property real dy: (((g - neutral) - (b - neutral)) * Math.sqrt(3) / 2) / range

    function emitFrom(x, y, m) {
        // x,y in unit disk; m master offset
        const len = Math.min(1, Math.sqrt(x * x + y * y))
        const ang = Math.atan2(y, x)
        const mag = len * range
        const cr = mag * Math.cos(ang) * 2 / 3
        const cg = mag * Math.cos(ang - 2 * Math.PI / 3) * 2 / 3
        const cb = mag * Math.cos(ang + 2 * Math.PI / 3) * 2 / 3
        wheel.moved(neutral + m + cr, neutral + m + cg, neutral + m + cb)
    }

    Label {
        Layout.alignment: Qt.AlignHCenter
        text: wheel.label
        font.pointSize: LingmoUI.Units.fontBody
        font.bold: true
        color: LingmoUI.Theme.textColor
    }

    Item {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: Math.min(150, wheel.width)
        Layout.preferredHeight: Layout.preferredWidth

        Canvas {
            id: canvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2, cy = height / 2, rad = Math.min(cx, cy) - 2
                const grad = ctx.createConicalGradient(cx, cy, 0)
                // hue wheel in the puck's direction: red right, yellow up-right, green up-left…
                const hues = [[0, "#ff4040"], [1 / 6, "#ffff40"], [2 / 6, "#40ff40"], [3 / 6, "#40ffff"], [4 / 6, "#4040ff"], [5 / 6, "#ff40ff"], [1, "#ff4040"]]
                for (let i = 0; i < hues.length; ++i)
                    grad.addColorStop(hues[i][0], hues[i][1])
                ctx.beginPath()
                ctx.arc(cx, cy, rad, 0, 2 * Math.PI)
                ctx.fillStyle = grad
                ctx.globalAlpha = 0.55
                ctx.fill()
                const shade = ctx.createRadialGradient(cx, cy, 0, cx, cy, rad)
                shade.addColorStop(0, LingmoUI.Theme.secondBackgroundColor)
                shade.addColorStop(1, "transparent")
                ctx.globalAlpha = 1
                ctx.fillStyle = shade
                ctx.fill()
                ctx.strokeStyle = LingmoUI.Theme.borderColor
                ctx.beginPath(); ctx.moveTo(cx - rad, cy); ctx.lineTo(cx + rad, cy); ctx.moveTo(cx, cy - rad); ctx.lineTo(cx, cy + rad); ctx.stroke()
            }
            Connections {
                target: LingmoUI.Theme
                function onDarkModeChanged() { canvas.requestPaint() }
            }
        }
        Rectangle {
            id: puck
            width: 14
            height: 14
            radius: 7
            color: "transparent"
            border.width: 2
            border.color: LingmoUI.Theme.textColor
            readonly property real rad: Math.min(parent.width, parent.height) / 2 - 2
            // canvas angles grow clockwise; y up is negative screen y
            x: parent.width / 2 + Math.max(-1, Math.min(1, wheel.dx)) * rad - width / 2
            y: parent.height / 2 - Math.max(-1, Math.min(1, wheel.dy)) * rad - height / 2
        }
        MouseArea {
            anchors.fill: parent
            function update(m) {
                const rad = Math.min(width, height) / 2 - 2
                let x = (m.x - width / 2) / rad
                let y = -(m.y - height / 2) / rad
                const len = Math.sqrt(x * x + y * y)
                if (len > 1) { x /= len; y /= len }
                wheel.emitFrom(x, y, wheel.master)
            }
            onPressed: function(m) { update(m) }
            onPositionChanged: function(m) { if (pressed) update(m) }
            onDoubleClicked: wheel.moved(wheel.neutral, wheel.neutral, wheel.neutral)
        }
    }

    Slider {
        Layout.fillWidth: true
        from: -wheel.range
        to: wheel.range
        value: wheel.master
        onMoved: wheel.emitFrom(wheel.dx, wheel.dy, value)
        ToolTip.visible: hovered || pressed
        ToolTip.text: qsTr("Master %1").arg(value.toFixed(3))
    }
    Label {
        Layout.alignment: Qt.AlignHCenter
        text: "R " + wheel.r.toFixed(2) + "  G " + wheel.g.toFixed(2) + "  B " + wheel.b.toFixed(2)
        color: LingmoUI.Theme.secondaryTextColor
        font.pointSize: LingmoUI.Units.fontCaption
        font.features: { "tnum": 1 }
    }
}
