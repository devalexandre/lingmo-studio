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

// Keyframe curve of one animatable parameter of the selected clip.
// Drag a point sideways to retime it, up/down to change its value;
// double-click adds a keyframe, right-click removes one.
ColumnLayout {
    id: kfe
    spacing: LingmoUI.Units.smallSpacing

    readonly property var sel: editor.selectedClip
    readonly property bool hasClip: sel && sel.id !== undefined
    readonly property var animEffects: hasClip ? sel.effects.filter(function(e) {
        const d = defOf(e.type)
        return d && d.params.some(function(p) { return p.animatable })
    }) : []
    property int effectChoice: 0
    property int paramChoice: 0
    readonly property var fx: animEffects.length > 0 ? animEffects[Math.min(effectChoice, animEffects.length - 1)] : null
    readonly property var fxDef: fx ? defOf(fx.type) : null
    readonly property var params: fxDef ? fxDef.params.filter(function(p) { return p.animatable }) : []
    readonly property var param: params.length > 0 ? params[Math.min(paramChoice, params.length - 1)] : null

    function defOf(type) {
        const defs = editor.effectDefs
        for (let i = 0; i < defs.length; ++i)
            if (defs[i].id === type)
                return defs[i]
        return null
    }
    onFxChanged: graph.requestPaint()
    onParamChanged: graph.requestPaint()

    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Keyframes")
            font.pointSize: LingmoUI.Units.fontBody
            font.bold: true
            color: LingmoUI.Theme.textColor
        }
        ComboBox {
            Layout.preferredWidth: 180
            model: kfe.animEffects.map(function(e) { return e.name })
            currentIndex: kfe.effectChoice
            onActivated: function(i) { kfe.effectChoice = i; kfe.paramChoice = 0 }
            enabled: kfe.animEffects.length > 0
        }
        ComboBox {
            Layout.preferredWidth: 160
            model: kfe.params.map(function(p) { return p.name })
            currentIndex: kfe.paramChoice
            onActivated: function(i) { kfe.paramChoice = i }
            enabled: kfe.params.length > 0
        }
        LingmoUI.IconButton {
            iconName: "keyframe-add"
            tipText: qsTr("Keyframe at playhead")
            enabled: kfe.fx !== null
            onClicked: editor.toggleKeyframe(kfe.sel.id, kfe.fx.index, editor.player.timelinePosition)
        }
        Item { Layout.fillWidth: true }
        Label {
            text: kfe.fx ? qsTr("%n keyframe(s)", "", kfe.fx.keyframes.length) : qsTr("Add Transform, Crop, Text or Volume to a clip to animate it")
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontCaption
        }
    }

    Canvas {
        id: graph
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 100
        readonly property int duration: kfe.hasClip ? kfe.sel.duration : 1
        readonly property real minV: kfe.param ? kfe.param.min : 0
        readonly property real maxV: kfe.param ? kfe.param.max : 1
        property int playhead: editor.player.timelinePosition
        property int dragging: -1
        onPlayheadChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: editor
            function onSelectedClipChanged() { graph.requestPaint() }
        }

        function xOf(f) { return 8 + f / Math.max(1, duration - 1) * (width - 16) }
        function yOf(v) { return 8 + (1 - (v - minV) / Math.max(1e-9, maxV - minV)) * (height - 16) }
        function frameAt(x) { return Math.round((x - 8) / (width - 16) * (duration - 1)) }
        function valueAt(y) { return minV + (1 - (y - 8) / (height - 16)) * (maxV - minV) }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = LingmoUI.Theme.alternateBackgroundColor
            ctx.fillRect(0, 0, width, height)
            if (!kfe.fx || !kfe.param)
                return
            ctx.strokeStyle = LingmoUI.Theme.borderColor
            ctx.beginPath(); ctx.moveTo(0, yOf(kfe.param.def)); ctx.lineTo(width, yOf(kfe.param.def)); ctx.stroke()
            const key = kfe.param.id
            const kfs = kfe.fx.keyframes
            ctx.strokeStyle = LingmoUI.Theme.highlightColor
            ctx.lineWidth = 2
            ctx.beginPath()
            if (kfs.length === 0) {
                const v = Number(kfe.fx.params[key])
                ctx.moveTo(0, yOf(v)); ctx.lineTo(width, yOf(v))
            } else {
                ctx.moveTo(0, yOf(Number(kfs[0].values[key] !== undefined ? kfs[0].values[key] : kfe.fx.params[key])))
                for (let i = 0; i < kfs.length; ++i) {
                    const v = Number(kfs[i].values[key] !== undefined ? kfs[i].values[key] : kfe.fx.params[key])
                    ctx.lineTo(xOf(kfs[i].frame), yOf(v))
                }
                const last = kfs[kfs.length - 1]
                ctx.lineTo(width, yOf(Number(last.values[key] !== undefined ? last.values[key] : kfe.fx.params[key])))
            }
            ctx.stroke()
            ctx.fillStyle = LingmoUI.Theme.orangeColor
            for (let i = 0; i < kfs.length; ++i) {
                const v = Number(kfs[i].values[key] !== undefined ? kfs[i].values[key] : kfe.fx.params[key])
                const x = xOf(kfs[i].frame), y = yOf(v)
                ctx.beginPath(); ctx.moveTo(x, y - 6); ctx.lineTo(x + 6, y); ctx.lineTo(x, y + 6); ctx.lineTo(x - 6, y); ctx.closePath(); ctx.fill()
            }
            const ph = playhead - kfe.sel.position
            if (ph >= 0 && ph < duration) {
                ctx.strokeStyle = LingmoUI.Theme.redColor
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(xOf(ph), 0); ctx.lineTo(xOf(ph), height); ctx.stroke()
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            enabled: kfe.fx !== null && kfe.param !== null
            function hit(m) {
                const kfs = kfe.fx.keyframes
                for (let i = 0; i < kfs.length; ++i) {
                    const v = Number(kfs[i].values[kfe.param.id] !== undefined ? kfs[i].values[kfe.param.id] : kfe.fx.params[kfe.param.id])
                    if (Math.abs(graph.xOf(kfs[i].frame) - m.x) < 8 && Math.abs(graph.yOf(v) - m.y) < 8)
                        return i
                }
                return -1
            }
            onPressed: function(m) {
                const i = hit(m)
                if (m.button === Qt.RightButton) {
                    if (i >= 0)
                        editor.removeKeyframe(kfe.sel.id, kfe.fx.index, i)
                    return
                }
                graph.dragging = i
                if (i < 0)
                    editor.player.seek(kfe.sel.position + Math.max(0, Math.min(graph.duration - 1, graph.frameAt(m.x))))
            }
            onPositionChanged: function(m) {
                if (graph.dragging < 0)
                    return
                const v = Math.max(kfe.param.min, Math.min(kfe.param.max, graph.valueAt(m.y)))
                editor.setKeyframeValue(kfe.sel.id, kfe.fx.index, graph.dragging, kfe.param.id, v)
                const f = Math.max(0, Math.min(graph.duration - 1, graph.frameAt(m.x)))
                if (f !== kfe.fx.keyframes[graph.dragging].frame)
                    editor.moveKeyframe(kfe.sel.id, kfe.fx.index, graph.dragging, f)
            }
            onReleased: graph.dragging = -1
            onDoubleClicked: function(m) {
                const f = Math.max(0, Math.min(graph.duration - 1, graph.frameAt(m.x)))
                editor.player.seek(kfe.sel.position + f)
                if (!editor.hasKeyframeAt(kfe.sel.id, kfe.fx.index, kfe.sel.position + f))
                    editor.toggleKeyframe(kfe.sel.id, kfe.fx.index, kfe.sel.position + f)
            }
        }
    }
}
