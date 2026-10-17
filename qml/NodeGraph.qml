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

// The selected clip's effect stack drawn as a node chain, Fusion style:
// MediaIn → effect → effect → MediaOut. Click a node to focus it in the
// inspector, toggle it with the eye, drag the canvas to pan.
Rectangle {
    id: graph

    property int focusIndex: -1
    signal focusRequested(int index)

    readonly property var sel: editor.selectedClip
    readonly property bool hasClip: sel && sel.id !== undefined
    readonly property var nodes: hasClip ? sel.effects : []
    readonly property int nodeW: 132
    readonly property int nodeH: 46
    readonly property int step: nodeW + 56

    color: StudioTheme.well
    radius: StudioTheme.radius
    clip: true

    // dotted grid
    Canvas {
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "rgba(255,255,255,0.06)"
            for (let x = 10; x < width; x += 20)
                for (let y = 10; y < height; y += 20)
                    ctx.fillRect(x, y, 1.5, 1.5)
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: Math.max(width, row.width + 80)
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}

        Item {
            id: row
            x: 40
            y: (flick.height - graph.nodeH) / 2
            width: (graph.nodes.length + 2) * graph.step - 56
            height: graph.nodeH

            // connections
            Canvas {
                id: wires
                x: 0
                y: -20
                width: row.width
                height: row.height + 40
                property int count: graph.nodes.length + 2
                onCountChanged: requestPaint()
                onWidthChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = "#5A5E68"
                    ctx.lineWidth = 2
                    const midY = 20 + graph.nodeH / 2
                    for (let i = 0; i < count - 1; ++i) {
                        const x0 = i * graph.step + graph.nodeW, x1 = (i + 1) * graph.step
                        ctx.beginPath()
                        ctx.moveTo(x0, midY)
                        ctx.bezierCurveTo(x0 + 28, midY, x1 - 28, midY, x1, midY)
                        ctx.stroke()
                        ctx.fillStyle = "#8A8E98"
                        ctx.beginPath(); ctx.arc(x0, midY, 3.5, 0, 2 * Math.PI); ctx.fill()
                        ctx.beginPath(); ctx.arc(x1, midY, 3.5, 0, 2 * Math.PI); ctx.fill()
                    }
                }
            }

            // MediaIn
            NodeBox {
                x: 0
                title: "MediaIn"
                subtitle: graph.hasClip ? (graph.sel.name || "") : ""
                tone: StudioTheme.trackVideo
                fixed: true
            }
            Repeater {
                model: graph.nodes
                NodeBox {
                    required property int index
                    required property var modelData
                    x: (index + 1) * graph.step
                    title: modelData.name
                    subtitle: ({ transform: qsTr("Transform"), color: qsTr("Color"), stylize: qsTr("Stylize"), key: qsTr("Keying"), text: qsTr("Text"), audio: qsTr("Audio") })[modelData.category] || ""
                    tone: ({ transform: StudioTheme.trackVideo, color: "#D8606A", stylize: StudioTheme.titleClipEdge, key: StudioTheme.trackAudio, text: StudioTheme.trackCaption, audio: StudioTheme.trackAudio })[modelData.category] || StudioTheme.textSecondary
                    enabledNode: modelData.enabled
                    current: graph.focusIndex === index
                    animated: modelData.animated === true
                    onPicked: graph.focusRequested(index)
                    onToggled: editor.setEffectEnabled(graph.sel.id, modelData.index, !modelData.enabled)
                }
            }
            NodeBox {
                x: (graph.nodes.length + 1) * graph.step
                title: "MediaOut"
                subtitle: qsTr("Timeline")
                tone: StudioTheme.accent
                fixed: true
            }
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        text: graph.hasClip ? (graph.nodes.length ? qsTr("Click a node to edit it in the inspector") : qsTr("Add an effect from the library to start the chain"))
                            : qsTr("Select a clip to see its effect chain")
        color: StudioTheme.textDisabled
        font.family: StudioTheme.font
        font.pixelSize: StudioTheme.fontSmall
    }

    component NodeBox: Rectangle {
        id: box
        property string title: ""
        property string subtitle: ""
        property color tone: StudioTheme.textSecondary
        property bool fixed: false
        property bool enabledNode: true
        property bool current: false
        property bool animated: false
        signal picked()
        signal toggled()

        width: graph.nodeW
        height: graph.nodeH
        radius: 6
        color: boxMouse.containsMouse ? "#2E3138" : "#272A30"
        border.width: current ? 2 : 1
        border.color: current ? StudioTheme.accent : StudioTheme.alpha(tone, 0.7)
        opacity: enabledNode ? 1 : 0.5
        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }

        Rectangle {
            width: 4
            height: parent.height - 12
            anchors.verticalCenter: parent.verticalCenter
            x: 6
            radius: 2
            color: box.tone
        }
        MouseArea {
            id: boxMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: !box.fixed
            cursorShape: box.fixed ? Qt.ArrowCursor : Qt.PointingHandCursor
            onClicked: box.picked()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: box.fixed ? 8 : 28
            spacing: 0
            Item { Layout.fillHeight: true }
            Text {
                Layout.fillWidth: true
                text: box.title
                color: StudioTheme.text
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontSmall
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: box.subtitle + (box.animated ? "  ◆" : "")
                color: StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontTiny
                elide: Text.ElideRight
            }
            Item { Layout.fillHeight: true }
        }
        StudioButton {
            visible: !box.fixed
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            iconName: box.enabledNode ? "eye" : "eye-off"
            size: 20
            iconSize: 12
            tip: box.enabledNode ? qsTr("Bypass") : qsTr("Enable")
            onClicked: box.toggled()
        }
    }
}
