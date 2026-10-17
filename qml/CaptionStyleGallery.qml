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

// Caption style presets with live previews. Each preview is rendered by the
// same libass renderer the editor and the export use (image://captionpreview),
// animated through the sample so karaoke and pop effects show.
GridView {
    id: gallery

    property string selectedId: "classic"
    property bool vertical: true
    property bool animate: visible
    // overrides applied to every preview (e.g. safe area of the platform)
    property var overrides: ({})
    signal chosen(var style)

    readonly property int previewW: vertical ? 126 : 240
    readonly property int previewH: vertical ? 224 : 135
    property int tick: 0 // 0..6 → 0..2400 ms of the sample

    cellWidth: Math.floor(width / Math.max(1, Math.floor(width / (previewW + LingmoUI.Units.largeSpacing))))
    cellHeight: previewH * (cellWidth - LingmoUI.Units.largeSpacing) / previewW + 44
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}
    model: editor.captionStyles

    Timer {
        interval: 400
        repeat: true
        running: gallery.animate
        onTriggered: gallery.tick = (gallery.tick + 1) % 7
    }

    function styleFor(base) {
        const s = Object.assign({}, base)
        for (const k in gallery.overrides)
            s[k] = gallery.overrides[k]
        delete s.name
        // thumbnails are small: show the text larger than on a full frame so the style reads
        s.size = (s.size || 6.5) * 1.35
        return s
    }

    delegate: Item {
        id: cell
        width: gallery.cellWidth
        height: gallery.cellHeight
        readonly property bool current: modelData.id === gallery.selectedId

        LingmoUI.Card {
            id: card
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.smallSpacing / 2 + 1
            clickable: true
            liftOnHover: true
            highlighted: cell.current
            onClicked: { gallery.selectedId = modelData.id; gallery.chosen(gallery.styleFor(modelData)) }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
                spacing: LingmoUI.Units.smallSpacing

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: LingmoUI.Theme.smallRadius
                    color: LingmoUI.Theme.alternateBackgroundColor
                    clip: true
                    // the next frame renders off screen; the shown one changes when it's ready (no flicker)
                    Image {
                        id: next
                        visible: false
                        asynchronous: true
                        cache: true
                        source: "image://captionpreview/" + (gallery.tick * 400) + "/" + (gallery.previewW * 2) + "x" + (gallery.previewH * 2) + "/"
                                + encodeURIComponent(JSON.stringify(gallery.styleFor(modelData)))
                        onStatusChanged: if (status === Image.Ready) shown.source = source
                    }
                    Image {
                        id: shown
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        cache: true
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: modelData.name
                        font.pointSize: LingmoUI.Units.fontBody
                        font.bold: cell.current
                        color: LingmoUI.Theme.textColor
                        elide: Text.ElideRight
                    }
                    LingmoUI.IconItem {
                        visible: cell.current
                        source: "checkmark"
                        implicitWidth: LingmoUI.Units.iconSizes.small
                        implicitHeight: LingmoUI.Units.iconSizes.small
                    }
                }
            }
        }
    }
}
