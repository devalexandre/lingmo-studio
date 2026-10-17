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

// Lift / gamma / gain / offset wheel. The puck pushes the balance towards a
// hue; the master slider below moves all three channels. Double-click the
// wheel (or the reset button) to neutralise it.
ColumnLayout {
    id: wheel

    property string label: ""
    property real neutral: 0     // 0 for lift and offset, 1 for gamma and gain
    property real range: 0.25    // largest per-channel offset at the rim
    property real r: neutral
    property real g: neutral
    property real b: neutral
    property int maxSize: 190
    signal moved(real r, real g, real b)

    spacing: 6

    readonly property real master: (r + g + b) / 3 - neutral
    // chroma offsets -> 2D (x right = red, rotating through yellow, green...)
    readonly property real dx: ((r - neutral) - ((g - neutral) + (b - neutral)) / 2) / range
    readonly property real dy: (((g - neutral) - (b - neutral)) * Math.sqrt(3) / 2) / range
    readonly property bool touched: Math.abs(r - neutral) > 1e-4 || Math.abs(g - neutral) > 1e-4 || Math.abs(b - neutral) > 1e-4

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

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Text {
            Layout.fillWidth: true
            text: wheel.label
            color: wheel.touched ? StudioTheme.text : StudioTheme.textSecondary
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontBody
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }
        StudioButton {
            iconName: "reset"
            size: 20
            iconSize: 12
            tip: qsTr("Reset %1").arg(wheel.label)
            enabled: wheel.touched
            opacity: wheel.touched ? 1 : 0.35
            onClicked: wheel.moved(wheel.neutral, wheel.neutral, wheel.neutral)
        }
    }

    // The disk takes the column's width but never feeds it back: its
    // implicit width stays 0, so the row layout cannot oscillate.
    Item {
        id: diskBox
        Layout.fillWidth: true
        Layout.preferredHeight: disk.width
        Item {
            id: disk
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(90, Math.min(wheel.maxSize, diskBox.width))
            height: width

        Canvas {
            id: canvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2, cy = height / 2, rad = Math.min(cx, cy) - 2
                // hue ring: red right, yellow up-right, green up-left… (counter-clockwise)
                const grad = ctx.createConicalGradient(cx, cy, 0)
                const hues = [[0, "#ff4848"], [1 / 6, "#ffd84a"], [2 / 6, "#4ade5a"], [3 / 6, "#4ad8e6"], [4 / 6, "#4a62ff"], [5 / 6, "#e64ae0"], [1, "#ff4848"]]
                for (let i = 0; i < hues.length; ++i)
                    grad.addColorStop(hues[i][0], hues[i][1])
                ctx.beginPath()
                ctx.arc(cx, cy, rad, 0, 2 * Math.PI)
                ctx.fillStyle = grad
                ctx.fill()
                // dark face with a soft colour bleed towards the rim
                const face = ctx.createRadialGradient(cx, cy, 0, cx, cy, rad - 5)
                face.addColorStop(0, "#1E1F23")
                face.addColorStop(0.72, "rgba(30,31,35,0.92)")
                face.addColorStop(1, "rgba(30,31,35,0.55)")
                ctx.beginPath()
                ctx.arc(cx, cy, rad - 5, 0, 2 * Math.PI)
                ctx.fillStyle = face
                ctx.fill()
                // crosshair and rings
                ctx.strokeStyle = "rgba(255,255,255,0.10)"
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(cx - rad + 8, cy); ctx.lineTo(cx + rad - 8, cy); ctx.moveTo(cx, cy - rad + 8); ctx.lineTo(cx, cy + rad - 8); ctx.stroke()
                ctx.beginPath(); ctx.arc(cx, cy, (rad - 5) / 2, 0, 2 * Math.PI); ctx.stroke()
            }
        }
        // line from the centre to the puck
        Canvas {
            anchors.fill: parent
            property real px: puck.x + puck.width / 2
            property real py: puck.y + puck.height / 2
            onPxChanged: requestPaint()
            onPyChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = "rgba(255,255,255,0.45)"
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(width / 2, height / 2); ctx.lineTo(px, py); ctx.stroke()
            }
        }
        Rectangle {
            id: puck
            width: 14
            height: 14
            radius: 7
            color: "transparent"
            border.width: 2
            border.color: "#FFFFFF"
            readonly property real rad: Math.min(parent.width, parent.height) / 2 - 7
            // canvas angles grow clockwise; y up is negative screen y
            x: parent.width / 2 + Math.max(-1, Math.min(1, wheel.dx)) * rad - width / 2
            y: parent.height / 2 - Math.max(-1, Math.min(1, wheel.dy)) * rad - height / 2
            scale: wheelMouse.pressed ? 1.2 : 1
            Behavior on scale { NumberAnimation { duration: StudioTheme.fast } }
        }
        MouseArea {
            id: wheelMouse
            anchors.fill: parent
            cursorShape: Qt.CrossCursor
            function update(m) {
                const rad = Math.min(width, height) / 2 - 7
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
    }

    // master: drag left/right
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 18
        Layout.leftMargin: 6
        Layout.rightMargin: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 4
            radius: 2
            color: StudioTheme.well
        }
        Rectangle {
            readonly property real centre: parent.width / 2
            readonly property real pos: (wheel.master / wheel.range * 0.5 + 0.5) * parent.width
            anchors.verticalCenter: parent.verticalCenter
            x: Math.min(centre, pos)
            width: Math.abs(pos - centre)
            height: 4
            color: StudioTheme.accent
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: Math.max(0, Math.min(parent.width, (wheel.master / wheel.range * 0.5 + 0.5) * parent.width)) - width / 2
            width: 10
            height: 14
            radius: 3
            color: masterMouse.pressed ? StudioTheme.text : "#C4C7CD"
        }
        MouseArea {
            id: masterMouse
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.SizeHorCursor
            hoverEnabled: true
            function update(x) {
                const v = Math.max(-1, Math.min(1, (x / width - 0.5) * 2)) * wheel.range
                wheel.emitFrom(wheel.dx, wheel.dy, v)
            }
            onPressed: function(m) { update(m.x) }
            onPositionChanged: function(m) { if (pressed) update(m.x) }
            onDoubleClicked: wheel.emitFrom(wheel.dx, wheel.dy, 0)
            ToolTip.visible: containsMouse || pressed
            ToolTip.text: qsTr("Master %1").arg(wheel.master.toFixed(3))
        }
    }
    // R G B readout
    Row {
        Layout.alignment: Qt.AlignHCenter
        spacing: 10
        Repeater {
            model: [{ c: "#FF6B6B", v: wheel.r }, { c: "#5FD97A", v: wheel.g }, { c: "#6B9BFF", v: wheel.b }]
            Text {
                text: modelData.v.toFixed(2)
                color: modelData.c
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontTiny
                font.features: { "tnum": 1 }
            }
        }
    }
}
