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

// Resolution and frame rate of the project (presets or custom).
Dialog {
    id: dialog
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Project settings")
    width: 440
    standardButtons: Dialog.Ok | Dialog.Cancel

    readonly property var presets: [
        { id: "1080p30", name: qsTr("1080p · 30 fps"), w: 1920, h: 1080, fps: 30 },
        { id: "1080p60", name: qsTr("1080p · 60 fps"), w: 1920, h: 1080, fps: 60 },
        { id: "1080p24", name: qsTr("1080p · 24 fps"), w: 1920, h: 1080, fps: 24 },
        { id: "4k30", name: qsTr("4K UHD · 30 fps"), w: 3840, h: 2160, fps: 30 },
        { id: "4k60", name: qsTr("4K UHD · 60 fps"), w: 3840, h: 2160, fps: 60 },
        { id: "720p30", name: qsTr("720p · 30 fps"), w: 1280, h: 720, fps: 30 },
        { id: "vertical1080", name: qsTr("Vertical 9:16 · 1080×1920"), w: 1080, h: 1920, fps: 30 },
        { id: "square1080", name: qsTr("Square 1:1 · 1080×1080"), w: 1080, h: 1080, fps: 30 },
        { id: "", name: qsTr("Custom"), w: 0, h: 0, fps: 0 }
    ]
    onAboutToShow: {
        width_.value = editor.projectWidth
        height_.value = editor.projectHeight
        fpsBox.editText = String(editor.fps)
        let idx = presets.length - 1
        for (let i = 0; i < presets.length; ++i)
            if (presets[i].id === editor.presetId)
                idx = i
        presetBox.currentIndex = idx
    }
    onAccepted: {
        const f = Number(fpsBox.editText.replace(",", "."))
        // NTSC rates as exact fractions
        const ntsc = { "23.976": [24000, 1001], "29.97": [30000, 1001], "59.94": [60000, 1001] }
        const key = f.toFixed(3).replace(/0+$/, "").replace(/\.$/, "")
        const frac = ntsc[key] || [Math.round(f), 1]
        editor.setProjectSettings(width_.value, height_.value, frac[0], frac[1])
    }

    contentItem: GridLayout {
        columns: 2
        columnSpacing: LingmoUI.Units.largeSpacing
        rowSpacing: LingmoUI.Units.smallSpacing

        Label { text: qsTr("Preset"); color: LingmoUI.Theme.secondaryTextColor }
        ComboBox {
            id: presetBox
            Layout.fillWidth: true
            model: dialog.presets.map(function(p) { return p.name })
            onActivated: function(i) {
                const p = dialog.presets[i]
                if (p.w > 0) {
                    width_.value = p.w
                    height_.value = p.h
                    fpsBox.editText = String(p.fps)
                }
            }
        }
        Label { text: qsTr("Width"); color: LingmoUI.Theme.secondaryTextColor }
        SpinBox { id: width_; from: 16; to: 8192; stepSize: 2; editable: true; Layout.fillWidth: true }
        Label { text: qsTr("Height"); color: LingmoUI.Theme.secondaryTextColor }
        SpinBox { id: height_; from: 16; to: 8192; stepSize: 2; editable: true; Layout.fillWidth: true }
        Label { text: qsTr("Frame rate"); color: LingmoUI.Theme.secondaryTextColor }
        ComboBox {
            id: fpsBox
            Layout.fillWidth: true
            editable: true
            model: ["23.976", "24", "25", "29.97", "30", "50", "59.94", "60"]
        }
        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Changing the frame rate keeps clips at the same times; undo history is cleared.")
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontCaption
        }
    }
}
