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

// Libraries: effects, transitions, titles and generators. A click applies
// to the selection (effects), the cut at the playhead (transitions) or adds
// a clip at the playhead (titles, colors).
Item {
    id: lib
    property int section: 0 // 0 effects, 1 transitions, 2 titles

    ColumnLayout {
        anchors.fill: parent
        spacing: LingmoUI.Units.smallSpacing

        LingmoUI.SegmentedControl {
            Layout.alignment: Qt.AlignHCenter
            model: [qsTr("Effects"), qsTr("Transitions"), qsTr("Titles")]
            currentIndex: lib.section
            onActivated: function(i) { lib.section = i }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            section.property: "category"
            section.delegate: Label {
                width: list.width
                text: ({ transform: qsTr("Transform"), color: qsTr("Color"), stylize: qsTr("Stylize"), key: qsTr("Keying"), text: qsTr("Text"), audio: qsTr("Audio"),
                         wipe: qsTr("Wipes and dissolves"), titles: qsTr("Title templates"), generators: qsTr("Generators"), pip: qsTr("Picture in picture") })[section] || section
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
                font.bold: true
                topPadding: LingmoUI.Units.smallSpacing
                leftPadding: LingmoUI.Units.smallSpacing
            }
            model: {
                if (lib.section === 0)
                    return editor.effectDefs.filter(function(d) { return !d.hidden }).map(function(d) {
                        return { category: d.category, name: d.name, id: d.id, available: d.available, kind: "effect" } })
                if (lib.section === 1)
                    return editor.transitionTypes.map(function(t) { return { category: "wipe", name: root.transitionLabel(t), id: t, available: true, kind: "transition" } })
                return [
                    { category: "titles", name: qsTr("Centered title"), id: "center", available: true, kind: "title" },
                    { category: "titles", name: qsTr("Lower third"), id: "lower-third", available: true, kind: "title" },
                    { category: "titles", name: qsTr("Headline"), id: "headline", available: true, kind: "title" },
                    { category: "titles", name: qsTr("Subtitle"), id: "subtitle", available: true, kind: "title" },
                    { category: "titles", name: qsTr("Credits"), id: "credits", available: true, kind: "title" },
                    { category: "pip", name: qsTr("Top right"), id: "top-right", available: true, kind: "pip" },
                    { category: "pip", name: qsTr("Top left"), id: "top-left", available: true, kind: "pip" },
                    { category: "pip", name: qsTr("Bottom right"), id: "bottom-right", available: true, kind: "pip" },
                    { category: "pip", name: qsTr("Bottom left"), id: "bottom-left", available: true, kind: "pip" },
                    { category: "generators", name: qsTr("Black"), id: "#000000", available: true, kind: "color" },
                    { category: "generators", name: qsTr("White"), id: "#ffffff", available: true, kind: "color" },
                    { category: "generators", name: qsTr("Accent color"), id: String(LingmoUI.Theme.highlightColor), available: true, kind: "color" }
                ]
            }
            delegate: LingmoUI.Card {
                width: list.width
                height: LingmoUI.Units.controlHeight
                clickable: modelData.available
                radius: LingmoUI.Theme.smallRadius
                opacity: modelData.available ? 1 : 0.5
                onClicked: {
                    if (modelData.kind === "effect")
                        editor.addEffectToSelection(modelData.id)
                    else if (modelData.kind === "transition")
                        editor.addTransitionAtPlayhead(modelData.id, Math.round(editor.fps))
                    else if (modelData.kind === "title")
                        editor.addTitle(modelData.id === "subtitle" ? qsTr("Subtitle text") : modelData.id === "credits" ? qsTr("Directed by\nYour Name") : "", modelData.id)
                    else if (modelData.kind === "pip")
                        editor.applyPictureInPicture(modelData.id)
                    else
                        editor.addColorClip(modelData.id)
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: LingmoUI.Units.largeSpacing
                    anchors.rightMargin: LingmoUI.Units.smallSpacing
                    Rectangle {
                        visible: modelData.kind === "color"
                        width: 14
                        height: 14
                        radius: 3
                        color: modelData.kind === "color" ? modelData.id : "transparent"
                        border.color: LingmoUI.Theme.borderColor
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.name + (modelData.available ? "" : " — " + qsTr("not installed"))
                        font.pointSize: LingmoUI.Units.fontBody
                        color: LingmoUI.Theme.textColor
                        elide: Text.ElideRight
                    }
                    LingmoUI.IconButton {
                        iconName: "list-add"
                        size: 24
                        enabled: modelData.available
                        tipText: modelData.kind === "effect" ? qsTr("Apply to the selected clips") : modelData.kind === "transition" ? qsTr("Apply at the cut nearest the playhead") : qsTr("Add at the playhead")
                        onClicked: parent.parent.clicked()
                    }
                }
            }
        }
    }
}
