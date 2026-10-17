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
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// One clip on a track. Drags only move this item visually; the edit is sent
// to the editor (one undo step) when the mouse is released.
Item {
    id: item

    property var owner
    property var info: ({})
    property int trackIndex: 0
    property string trackKind: "video"
    property bool locked: false
    property bool dimmed: false
    readonly property bool selected: editor.selection.indexOf(info.id) >= 0
    readonly property real ppf: owner.ppf

    // live drag state
    property real moveDX: 0
    property real moveDY: 0
    property int targetTrack: trackIndex
    property real trimLeftDX: 0
    property real trimRightDX: 0
    property int slipFrames: 0
    property bool interacting: false

    x: info.position * ppf + moveDX + trimLeftDX
    width: Math.max(2, info.duration * ppf - trimLeftDX + trimRightDX)
    z: interacting ? 10 : (selected ? 2 : 1)
    opacity: dimmed ? 0.45 : 1

    readonly property color baseColor: {
        switch (info.mediaKind) {
        case "title": return LingmoUI.Theme.purpleColor
        case "compound": return LingmoUI.Theme.orangeColor
        case "audio": return LingmoUI.Theme.greenColor
        case "image": return LingmoUI.Theme.blueColor
        }
        if (trackKind === "subtitle")
            return LingmoUI.Theme.pinkColor
        if (trackKind === "audio")
            return LingmoUI.Theme.greenColor
        return LingmoUI.Theme.blueColor
    }
    readonly property bool showThumbs: trackKind !== "audio" && (info.mediaKind === "video" || info.mediaKind === "image") && height > 36
    readonly property bool showWave: (trackKind === "audio" || info.mediaKind === "audio") && clip.hasAudio !== false

    transform: Translate { y: item.moveDY }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: LingmoUI.Theme.tinyRadius
        color: info.kind === "color" && info.color ? info.color : Qt.rgba(item.baseColor.r, item.baseColor.g, item.baseColor.b, 0.55)
        border.width: item.selected ? 2 : 1
        border.color: item.selected ? LingmoUI.Theme.highlightColor : Qt.rgba(item.baseColor.r, item.baseColor.g, item.baseColor.b, 0.95)
        clip: true

        // thumbnails along the clip
        Row {
            visible: item.showThumbs
            anchors.fill: parent
            anchors.topMargin: 16
            anchors.margins: 1
            readonly property real tileW: Math.max(height * editor.projectWidth / editor.projectHeight, item.width / 24)
            Repeater {
                model: item.showThumbs ? Math.min(24, Math.ceil(item.width / parent.tileW)) : 0
                Image {
                    width: parent.tileW
                    height: parent.height
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    sourceSize.height: 72
                    readonly property int frame: item.info.in + Math.round(index * parent.tileW / item.ppf)
                    // quantize so zooming reuses cached thumbnails
                    source: "image://thumb/" + item.info.mediaId + "/" + (Math.floor(frame / 15) * 15)
                    opacity: 0.85
                }
            }
        }
        // waveform
        Image {
            visible: item.showWave
            anchors.fill: parent
            anchors.topMargin: item.trackKind === "audio" ? 14 : parent.height * 0.55
            fillMode: Image.Stretch
            asynchronous: true
            cache: false
            sourceSize: Qt.size(Math.max(8, Math.min(2048, Math.round(item.width))), Math.max(8, Math.round(height)))
            source: visible ? "image://wave/" + item.info.mediaId + "/" + item.info.in + "/" + item.info.out + "/" + String(LingmoUI.Theme.textColor).substring(1, 7) : ""
        }

        // fades
        Canvas {
            anchors.fill: parent
            property int fi: item.info.fadeIn
            property int fo: item.info.fadeOut
            property real scale: item.ppf
            onFiChanged: requestPaint()
            onFoChanged: requestPaint()
            onScaleChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = Qt.rgba(0, 0, 0, 0.35)
                if (fi > 0) {
                    ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(fi * scale, 0); ctx.lineTo(0, height); ctx.closePath(); ctx.fill()
                }
                if (fo > 0) {
                    ctx.beginPath(); ctx.moveTo(width, 0); ctx.lineTo(width - fo * scale, 0); ctx.lineTo(width, height); ctx.closePath(); ctx.fill()
                }
            }
        }

        // transition from the previous clip
        Rectangle {
            visible: item.info.transitionIn > 0
            width: item.info.transitionIn * item.ppf
            height: parent.height
            color: Qt.rgba(LingmoUI.Theme.textColor.r, LingmoUI.Theme.textColor.g, LingmoUI.Theme.textColor.b, 0.18)
            border.color: LingmoUI.Theme.textColor
            border.width: 1
            Canvas {
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = LingmoUI.Theme.textColor
                    ctx.globalAlpha = 0.5
                    ctx.beginPath(); ctx.moveTo(0, height); ctx.lineTo(width, 0); ctx.moveTo(0, 0); ctx.lineTo(width, height); ctx.stroke()
                }
                onWidthChanged: requestPaint()
            }
            ToolTip.visible: transMouse.containsMouse
            ToolTip.text: owner.transitionName(item.info.transition) + " · " + editor.formatDuration(item.info.transitionIn) + " (" + item.info.transitionIn + "f)"
            MouseArea {
                id: transMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: owner.openClipMenu(item.info, item.info.position)
            }
        }

        // label row
        Row {
            x: 4 + (item.info.transitionIn > 0 ? item.info.transitionIn * item.ppf : 0)
            y: 1
            spacing: 4
            Label {
                text: item.info.name || ""
                color: LingmoUI.Theme.textColor
                font.pointSize: LingmoUI.Units.fontCaption
                elide: Text.ElideRight
                width: Math.min(implicitWidth, Math.max(0, item.width - 60))
            }
            Label {
                visible: item.info.speed !== 1 || item.info.ramp === true
                text: item.info.ramp ? qsTr("ramp") : (item.info.speed < 0 ? qsTr("rev") : item.info.speed + "×")
                color: LingmoUI.Theme.orangeColor
                font.pointSize: LingmoUI.Units.fontCaption
                font.bold: true
            }
            Label {
                visible: item.info.effects > 0
                text: "fx"
                color: LingmoUI.Theme.highlightColor
                font.pointSize: LingmoUI.Units.fontCaption
                font.bold: true
            }
            Label {
                visible: item.info.freeze === true
                text: qsTr("hold")
                color: LingmoUI.Theme.orangeColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
            Label {
                visible: item.info.linked === true
                text: "⛓"
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
        }
        Label {
            visible: item.slipFrames !== 0
            anchors.centerIn: parent
            text: (item.slipFrames > 0 ? "+" : "") + item.slipFrames + "f"
            color: LingmoUI.Theme.textColor
            font.pointSize: LingmoUI.Units.fontBody
            font.bold: true
        }
    }

    // move / razor / slip / slide
    MouseArea {
        id: mover
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        enabled: !item.locked
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: owner.tool === "razor" ? Qt.IBeamCursor : (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
        property real startX: 0
        property real startY: 0
        property bool moved: false

        function frameAtMouse(mx) { return item.info.position + Math.round((mx + anchors.leftMargin) / item.ppf) }

        onPressed: function(m) {
            if (m.button === Qt.RightButton) {
                if (!item.selected)
                    editor.select(item.info.id)
                owner.openClipMenu(item.info, frameAtMouse(m.x))
                return
            }
            if (owner.tool === "razor") {
                editor.splitClip(item.info.id, owner.snapping ? owner.snap(frameAtMouse(m.x), item.info.id) : frameAtMouse(m.x))
                return
            }
            editor.select(item.info.id, (m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0)
            const p = mapToItem(null, m.x, m.y)
            startX = p.x
            startY = p.y
            moved = false
            item.interacting = true
        }
        onPositionChanged: function(m) {
            if (!pressed || owner.tool === "razor" || !item.interacting)
                return
            const p = mapToItem(null, m.x, m.y)
            const dx = p.x - startX
            const dy = p.y - startY
            if (!moved && Math.abs(dx) < 3 && Math.abs(dy) < 3)
                return
            moved = true
            const df = Math.round(dx / item.ppf)
            if (owner.tool === "slip") {
                item.slipFrames = -df
                return
            }
            if (owner.tool === "slide") {
                item.moveDX = df * item.ppf
                return
            }
            // select/move: snap the nearer edge
            let pos = item.info.position + df
            const snappedStart = owner.snap(pos, item.info.id)
            const snappedEnd = owner.snap(pos + item.info.duration, item.info.id) - item.info.duration
            pos = Math.abs(snappedStart - pos) <= Math.abs(snappedEnd - pos) ? snappedStart : snappedEnd
            pos = Math.max(0, pos)
            item.moveDX = (pos - item.info.position) * item.ppf
            // vertical: another track of the same kind
            const lanePoint = item.parent.mapFromItem(null, p.x, p.y)
            const yInColumn = item.parent.y + lanePoint.y
            const t = owner.trackAt(yInColumn)
            if (t >= 0 && owner.model.trackKind(t) === item.trackKind) {
                item.targetTrack = t
                let off = 0
                const a = Math.min(t, item.trackIndex), b = Math.max(t, item.trackIndex)
                for (let i = a; i < b; ++i)
                    off += owner.trackHeight(owner.model.trackKind(i))
                item.moveDY = t > item.trackIndex ? off : -off
            }
        }
        onReleased: function(m) {
            if (!item.interacting)
                return
            item.interacting = false
            const df = Math.round(item.moveDX / item.ppf)
            const slip = item.slipFrames
            const target = item.targetTrack
            item.moveDX = 0
            item.moveDY = 0
            item.slipFrames = 0
            item.targetTrack = item.trackIndex
            if (!moved)
                return
            if (owner.tool === "slip")
                editor.slipClip(item.info.id, slip)
            else if (owner.tool === "slide")
                editor.slideClip(item.info.id, df)
            else if (df !== 0 || target !== item.trackIndex)
                editor.moveClip(item.info.id, target, item.info.position + df)
        }
        onDoubleClicked: {
            if (item.info.kind === "compound")
                editor.openCompound(item.info.id)
            else if (item.info.kind === "media") {
                editor.mediaBin.openInSource(item.info.mediaId)
                editor.player.sourceIn = item.info.in
                editor.player.sourceOut = item.info.out
                editor.player.seek(item.info.in)
            }
        }
    }

    // trim / roll handles
    Repeater {
        model: 2
        MouseArea {
            readonly property bool isLeft: index === 0
            x: isLeft ? 0 : item.width - width
            width: Math.min(8, item.width / 3)
            height: item.height
            enabled: !item.locked && owner.tool !== "razor"
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            property real startX: 0
            Rectangle {
                anchors.fill: parent
                visible: parent.containsMouse || parent.pressed
                color: owner.tool === "roll" ? LingmoUI.Theme.orangeColor : LingmoUI.Theme.highlightColor
                opacity: 0.8
                radius: 2
            }
            onPressed: function(m) {
                startX = mapToItem(null, m.x, 0).x
                item.interacting = true
                editor.select(item.info.id)
            }
            onPositionChanged: function(m) {
                if (!pressed)
                    return
                const dx = mapToItem(null, m.x, 0).x - startX
                let edge = (isLeft ? item.info.position : item.info.position + item.info.duration) + Math.round(dx / item.ppf)
                edge = owner.snap(edge, item.info.id)
                const d = (edge - (isLeft ? item.info.position : item.info.position + item.info.duration)) * item.ppf
                if (isLeft)
                    item.trimLeftDX = Math.min(d, (item.info.duration - 1) * item.ppf)
                else
                    item.trimRightDX = Math.max(d, -(item.info.duration - 1) * item.ppf)
            }
            onReleased: {
                item.interacting = false
                const df = Math.round((isLeft ? item.trimLeftDX : item.trimRightDX) / item.ppf)
                item.trimLeftDX = 0
                item.trimRightDX = 0
                if (df === 0)
                    return
                if (owner.tool === "roll") {
                    if (isLeft) {
                        const prev = owner.clipEndingAt(item.trackIndex, item.info.position)
                        if (prev.length)
                            editor.rollEdit(prev, item.info.position + df)
                    } else {
                        editor.rollEdit(item.info.id, item.info.position + item.info.duration + df)
                    }
                } else if (isLeft) {
                    editor.trimClip(item.info.id, true, item.info.position + df, root.rippleMode)
                } else {
                    editor.trimClip(item.info.id, false, item.info.position + item.info.duration + df, root.rippleMode)
                }
            }
        }
    }

    // fade handles (top corners)
    Repeater {
        model: item.trackKind === "subtitle" || item.width < 40 ? 0 : 2
        Rectangle {
            readonly property bool fadeInHandle: index === 0
            width: 9
            height: 9
            radius: 4.5
            y: 2
            x: fadeInHandle ? Math.min(item.width - 20, 8 + item.info.fadeIn * item.ppf) - 4
                            : Math.max(20, item.width - 8 - item.info.fadeOut * item.ppf) - 5
            color: LingmoUI.Theme.textColor
            opacity: fadeMouse.containsMouse || fadeMouse.pressed || item.selected ? 0.9 : 0
            MouseArea {
                id: fadeMouse
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: Qt.SizeHorCursor
                enabled: !item.locked
                onPositionChanged: function(m) {
                    if (!pressed)
                        return
                    const p = mapToItem(item, m.x, m.y)
                    const frames = parent.fadeInHandle ? Math.round((p.x - 8) / item.ppf) : Math.round((item.width - 8 - p.x) / item.ppf)
                    editor.setClipProperty(item.info.id, parent.fadeInHandle ? "fadeIn" : "fadeOut", Math.max(0, frames))
                }
            }
            ToolTip.visible: fadeMouse.pressed
            ToolTip.text: (fadeInHandle ? qsTr("Fade in") : qsTr("Fade out")) + " " + (fadeInHandle ? item.info.fadeIn : item.info.fadeOut) + "f"
        }
    }
}
