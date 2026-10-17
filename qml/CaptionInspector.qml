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

// Editing one caption: its text, its words (timing, split, keyword), the
// speaker and merging with the next caption. Edges are dragged on the timeline.
LingmoUI.Card {
    id: card
    property var info: ({})
    property int word: -1
    signal styleRequested()

    implicitHeight: col.implicitHeight + LingmoUI.Units.largeSpacing * 2
    onInfoChanged: if (!info.words || word >= info.words.length) word = -1

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Caption")
                font.pointSize: LingmoUI.Units.fontSubheading
                font.bold: true
                color: LingmoUI.Theme.textColor
            }
            LingmoUI.ActionButton {
                text: qsTr("Style…")
                iconName: "format-text-color"
                onClicked: card.styleRequested()
            }
        }
        TextArea {
            id: textEdit
            Layout.fillWidth: true
            wrapMode: TextEdit.Wrap
            text: card.info.text || ""
            font.pointSize: LingmoUI.Units.fontBody
            placeholderText: qsTr("Caption text")
            onEditingFinished: if (text !== card.info.text) editor.setCaptionText(card.info.id, text)
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Fixing the spelling keeps every word's timing. Adding or removing words spreads them over the spoken time.")
            font.pointSize: LingmoUI.Units.fontCaption
            color: LingmoUI.Theme.secondaryTextColor
        }

        Label { text: qsTr("Words"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: card.info.words || []
                Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool current: card.word === index
                    width: chip.implicitWidth + LingmoUI.Units.smallSpacing * 2
                    height: chip.implicitHeight + 6
                    radius: LingmoUI.Theme.tinyRadius
                    color: current ? LingmoUI.Theme.highlightColor : (chipMouse.containsMouse ? LingmoUI.Theme.hoverColor : LingmoUI.Theme.alternateBackgroundColor)
                    opacity: modelData.visible ? 1 : 0.55
                    Behavior on color { ColorAnimation { duration: LingmoUI.Units.veryShortDuration } }
                    Label {
                        id: chip
                        anchors.centerIn: parent
                        text: modelData.text
                        font.pointSize: LingmoUI.Units.fontCaption
                        font.bold: modelData.emphasis
                        color: parent.current ? LingmoUI.Theme.highlightedTextColor : LingmoUI.Theme.textColor
                    }
                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            card.word = parent.current ? -1 : parent.index
                            editor.player.seek(editor.framesFromSeconds(parent.modelData.startMs / 1000))
                        }
                    }
                }
            }
        }
        // the selected word
        RowLayout {
            Layout.fillWidth: true
            visible: card.word >= 0 && card.info.words && card.word < card.info.words.length
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: visible ? qsTr("%1 → %2").arg(editor.timecode(editor.framesFromSeconds(card.info.words[card.word].startMs / 1000)))
                                               .arg(editor.timecode(editor.framesFromSeconds(card.info.words[card.word].endMs / 1000))) : ""
                font.pointSize: LingmoUI.Units.fontCaption
                color: LingmoUI.Theme.secondaryTextColor
            }
            LingmoUI.IconButton { iconName: "go-previous"; tipText: qsTr("Earlier (50 ms)"); onClicked: editor.nudgeCaptionWord(card.info.id, card.word, -50) }
            LingmoUI.IconButton { iconName: "go-next"; tipText: qsTr("Later (50 ms)"); onClicked: editor.nudgeCaptionWord(card.info.id, card.word, 50) }
            LingmoUI.IconButton {
                iconName: "format-text-bold"
                checkable: true
                checked: card.word >= 0 && card.info.words && card.info.words[card.word] && card.info.words[card.word].emphasis === true
                tipText: qsTr("Keyword (colored in the Podcast style)")
                onClicked: editor.toggleCaptionEmphasis(card.info.id, card.word)
            }
            LingmoUI.IconButton {
                iconName: "edit-cut"
                enabled: card.word > 0
                tipText: qsTr("New caption from this word")
                onClicked: { editor.splitCaption(card.info.id, card.word); card.word = -1 }
            }
        }

        Label { text: qsTr("Speaker"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
        LingmoUI.SegmentedControl {
            Layout.fillWidth: true
            fillWidth: true
            model: [qsTr("Person 1"), qsTr("Person 2")]
            currentIndex: (card.info.speaker || 1) - 1
            onActivated: function(i) { editor.setCaptionSpeaker(card.info.id, i + 1) }
        }
        LingmoUI.ActionButton {
            Layout.fillWidth: true
            text: qsTr("Merge with the next caption")
            iconName: "merge"
            onClicked: editor.mergeCaption(card.info.id)
        }
    }
}
