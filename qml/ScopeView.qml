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

// Shows one scope rendered by Scopes (C++), refreshed with each analysis.
Rectangle {
    id: view
    property string kind: "waveform"
    radius: LingmoUI.Theme.smallRadius
    color: Qt.rgba(0, 0, 0, 0.85) // scopes read best on black
    clip: true

    Image {
        id: img
        anchors.fill: parent
        anchors.margins: 6
        fillMode: view.kind === "vectorscope" ? Image.PreserveAspectFit : Image.Stretch
        smooth: true
        cache: false
        asynchronous: false
        source: "image://scope/" + view.kind + "/" + editor.scopes.serial
    }
    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        text: ({ waveform: qsTr("Waveform"), parade: qsTr("RGB parade"), vectorscope: qsTr("Vectorscope"), histogram: qsTr("Histogram") })[view.kind]
        color: LingmoUI.Theme.secondaryTextColor
        font.pointSize: LingmoUI.Units.fontCaption
    }
}
