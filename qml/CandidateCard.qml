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

// One suggested cut: thumbnail, time range, hook title, score and why,
// with play, select and trim (edges snap to word boundaries).
LingmoUI.Card {
    id: card
    property var cut: ({})
    property bool playing: false
    signal playRequested()

    hoverEnabled: true
    highlighted: cut.selected === true
    implicitHeight: row.implicitHeight + LingmoUI.Units.largeSpacing * 2

    function scoreTone(s) {
        return s >= 70 ? LingmoUI.Theme.greenColor : s >= 45 ? LingmoUI.Theme.orangeColor : LingmoUI.Theme.greyColor
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.largeSpacing

        // vertical thumbnail: what the 9:16 crop will roughly show
        Rectangle {
            Layout.preferredWidth: 90
            Layout.preferredHeight: 160
            Layout.alignment: Qt.AlignTop
            radius: LingmoUI.Theme.smallRadius
            color: Qt.rgba(0, 0, 0, 1) // video surround
            clip: true
            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize: Qt.size(284, 160)
                source: card.cut.thumbnail || ""
            }
            LingmoUI.IconButton {
                anchors.centerIn: parent
                iconName: card.playing ? "media-playback-pause" : "media-playback-start"
                round: true
                size: 36
                tipText: card.playing ? qsTr("Pause") : qsTr("Play this cut")
                onClicked: card.playRequested()
            }
            Label {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: 4
                text: Math.round(card.cut.seconds || 0) + " s"
                font.pointSize: LingmoUI.Units.fontCaption
                color: LingmoUI.Theme.highlightedTextColor
                background: Rectangle { color: Qt.rgba(0, 0, 0, 0.6); radius: LingmoUI.Theme.tinyRadius }
                leftPadding: 4; rightPadding: 4
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: LingmoUI.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                CheckBox {
                    checked: card.cut.selected === true
                    onToggled: editor.shorts.setSelected(card.cut.id, checked)
                }
                Label {
                    Layout.fillWidth: true
                    text: card.cut.title || ""
                    font.pointSize: LingmoUI.Units.fontSubheading
                    font.bold: true
                    color: LingmoUI.Theme.textColor
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                LingmoUI.Badge {
                    text: qsTr("%1 pts").arg(Math.round(card.cut.score || 0))
                    tone: card.scoreTone(card.cut.score || 0)
                }
            }
            RowLayout {
                spacing: LingmoUI.Units.smallSpacing
                LingmoUI.IconItem {
                    source: "chronometer"
                    implicitWidth: LingmoUI.Units.iconSizes.small
                    implicitHeight: LingmoUI.Units.iconSizes.small
                }
                Label {
                    text: card.cut.range || ""
                    font.pointSize: LingmoUI.Units.fontCaption
                    color: LingmoUI.Theme.secondaryTextColor
                }
                LingmoUI.Badge {
                    visible: card.cut.llm === true
                    text: qsTr("Local AI")
                    subtle: true
                }
            }
            Label {
                Layout.fillWidth: true
                text: card.cut.reason || ""
                font.pointSize: LingmoUI.Units.fontCaption
                color: LingmoUI.Theme.secondaryTextColor
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: "“" + (card.cut.text || "") + "”"
                font.pointSize: LingmoUI.Units.fontCaption
                font.italic: true
                color: LingmoUI.Theme.textColor
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            // trim: each edge moves to the previous/next word
            RowLayout {
                spacing: 2
                Label { text: qsTr("Start"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                LingmoUI.IconButton { iconName: "go-previous"; size: 24; tipText: qsTr("Start earlier"); onClicked: editor.shorts.trim(card.cut.id, true, -1000) }
                LingmoUI.IconButton { iconName: "go-next"; size: 24; tipText: qsTr("Start later"); onClicked: editor.shorts.trim(card.cut.id, true, 1000) }
                Item { width: LingmoUI.Units.largeSpacing }
                Label { text: qsTr("End"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                LingmoUI.IconButton { iconName: "go-previous"; size: 24; tipText: qsTr("End earlier"); onClicked: editor.shorts.trim(card.cut.id, false, -1000) }
                LingmoUI.IconButton { iconName: "go-next"; size: 24; tipText: qsTr("End later"); onClicked: editor.shorts.trim(card.cut.id, false, 1000) }
            }
        }
    }
}
