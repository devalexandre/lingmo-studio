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

// Vertical channel fader (0..1), with an optional dB scale for the
// -60…+12 dB mapping the mixer uses. Double-click resets.
Item {
    id: fader

    property real value: 0.833
    property bool showScale: true
    readonly property bool hovered: mouse.containsMouse
    signal moved(real value)
    signal reset()

    implicitWidth: 28

    readonly property real travel: height - cap.height

    // scale
    Repeater {
        model: fader.showScale ? [12, 6, 0, -6, -12, -24, -40, -60] : []
        Item {
            readonly property real pos: (modelData + 60) / 72
            x: 0
            y: fader.travel * (1 - pos) + cap.height / 2 - 5
            width: fader.width
            height: 10
            Text {
                x: 0
                anchors.verticalCenter: parent.verticalCenter
                text: modelData > 0 ? "+" + modelData : modelData
                color: modelData === 0 ? StudioTheme.textSecondary : StudioTheme.textDisabled
                font.family: StudioTheme.font
                font.pixelSize: 8
            }
        }
    }
    // slot
    Rectangle {
        id: slot
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: fader.showScale ? 7 : 0
        y: cap.height / 2
        width: 4
        height: fader.travel
        radius: 2
        color: StudioTheme.well
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.height * fader.value
            radius: 2
            color: StudioTheme.alpha(StudioTheme.accent, 0.55)
        }
    }
    // cap
    Rectangle {
        id: cap
        anchors.horizontalCenter: slot.horizontalCenter
        y: fader.travel * (1 - fader.value)
        width: 22
        height: 12
        radius: 3
        color: mouse.pressed ? StudioTheme.text : mouse.containsMouse ? "#D4D6DB" : "#B8BBC2"
        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
        Rectangle { anchors.centerIn: parent; width: parent.width - 6; height: 1; color: "#16171A" }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        property real grab: 0
        function valueAt(y) { return Math.max(0, Math.min(1, 1 - (y - grab) / Math.max(1, fader.travel))) }
        onPressed: function(m) {
            const onCap = m.y >= cap.y && m.y <= cap.y + cap.height
            grab = onCap ? m.y - cap.y : cap.height / 2
            if (!onCap)
                fader.moved(valueAt(m.y))
        }
        onPositionChanged: function(m) { if (pressed) fader.moved(valueAt(m.y)) }
        onDoubleClicked: fader.reset()
        onWheel: function(w) { fader.moved(Math.max(0, Math.min(1, fader.value + (w.angleDelta.y > 0 ? 0.02 : -0.02)))) }
    }
}
