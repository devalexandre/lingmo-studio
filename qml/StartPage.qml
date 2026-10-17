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

// Start: the project manager. Open a long video to cut it, reopen a recent
// project, or start one from a template.
Item {
    id: start
    signal done()
    signal openRequested()
    signal shortsRequested()

    // templates in the order people reach for them; Square is a size preset
    readonly property var templateCards: {
        const byId = {}
        editor.templates.forEach(function(t) { byId[t.id] = t })
        const list = []
        if (byId.youtube) list.push({ id: "youtube", template: true, name: qsTr("YouTube"), shape: "16:9", w: 16, h: 9, description: byId.youtube.description })
        if (byId.shorts) list.push({ id: "shorts", template: true, name: qsTr("Shorts / Reels"), shape: "9:16", w: 9, h: 16, description: byId.shorts.description })
        list.push({ id: "square1080", template: false, name: qsTr("Square"), shape: "1:1", w: 1, h: 1, description: qsTr("1080×1080 for feeds and carousels") })
        editor.templates.forEach(function(t) {
            if (t.id !== "youtube" && t.id !== "shorts")
                list.push({ id: t.id, template: true, name: t.name, shape: t.id === "film4k" ? "4K" : "16:9", w: 16, h: 9, description: t.description })
        })
        return list
    }

    component SectionTitle: RowLayout {
        property string text: ""
        property string hint: ""
        Layout.fillWidth: true
        Layout.topMargin: 8
        spacing: 10
        Text {
            text: parent.text
            color: StudioTheme.text
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontTitle
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: parent.hint
            color: StudioTheme.textSecondary
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontSmall
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: StudioTheme.radius
        color: StudioTheme.panel
        border.width: 1
        border.color: StudioTheme.border
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 1
        contentWidth: availableWidth
        clip: true

        // Sized from the page, not from availableWidth: the cards' height
        // follows their width, so tying it to the scroll bar's presence
        // makes the bar toggle forever at some window sizes.
        ColumnLayout {
            id: content
            x: Math.max(24, (start.width - 16 - width) / 2)
            width: Math.min(start.width - 72, 1240)
            spacing: 14

            Item { Layout.preferredHeight: 10 }

            // ---------------------------------------------------- header
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Image {
                    source: "qrc:/data/lingmo-studio.svg"
                    sourceSize: Qt.size(36, 36)
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                }
                ColumnLayout {
                    spacing: 0
                    Text {
                        text: qsTr("Project Manager")
                        color: StudioTheme.text
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontHero
                        font.weight: Font.Bold
                    }
                    Text {
                        text: qsTr("Projects are MLT files, so Shotcut and melt can open them too.")
                        color: StudioTheme.textSecondary
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontBody
                    }
                }
                Item { Layout.fillWidth: true }
                StudioButton { text: qsTr("Open…"); iconName: "folder"; flat: false; size: 32; shortcut: "Ctrl+O"; tip: qsTr("Open project"); onClicked: start.openRequested() }
                StudioButton { text: qsTr("Continue without a template"); size: 32; onClicked: start.done() }
            }

            // ---------------------------------------------------- hero: cut a long video
            AbstractButton {
                id: hero
                Layout.fillWidth: true
                Layout.preferredHeight: 116
                Layout.topMargin: 6
                hoverEnabled: true
                onClicked: start.shortsRequested()
                background: Rectangle {
                    radius: StudioTheme.radius + 4
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: hero.hovered ? "#2D6FE0" : "#2560C8" }
                        GradientStop { position: 1; color: hero.hovered ? "#1E2D52" : "#1B2540" }
                    }
                    Behavior on opacity { NumberAnimation { duration: StudioTheme.fast } }
                }
                contentItem: RowLayout {
                    spacing: 18
                    Rectangle {
                        Layout.leftMargin: 22
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 64
                        radius: 16
                        color: Qt.rgba(1, 1, 1, 0.14)
                        StudioIcon { anchors.centerIn: parent; name: "shorts"; size: 34; color: "#FFFFFF" }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: qsTr("Open video and generate cuts")
                            color: "#FFFFFF"
                            font.family: StudioTheme.font
                            font.pixelSize: 20
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Vertical clips for TikTok, Reels and Shorts, with captions, found by local AI")
                            color: Qt.rgba(1, 1, 1, 0.78)
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontLarge
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        Layout.rightMargin: 24
                        Layout.preferredWidth: heroLabel.implicitWidth + 36
                        Layout.preferredHeight: 38
                        radius: 19
                        color: hero.hovered ? "#FFFFFF" : Qt.rgba(1, 1, 1, 0.92)
                        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                        Text {
                            id: heroLabel
                            anchors.centerIn: parent
                            text: qsTr("Choose a video")
                            color: "#1A3E86"
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontLarge
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            // ---------------------------------------------------- recent projects
            SectionTitle { text: qsTr("Recent projects"); hint: editor.recentProjects.length ? "" : qsTr("No recent projects") }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                // card sizes come from the column's fixed width, never from the
                // Flow's own (layout-assigned) width: that feedback loops forever
                readonly property int columns: Math.max(2, Math.floor((content.width + spacing) / (236 + spacing)))
                readonly property real cardW: Math.floor((content.width - spacing * (columns - 1)) / columns)

                // new project
                AbstractButton {
                    id: newCard
                    width: parent.cardW
                    height: width * 9 / 16 + 54
                    hoverEnabled: true
                    onClicked: { editor.newProject("1080p30"); start.done() }
                    ToolTip.visible: hovered
                    ToolTip.text: StudioTheme.tip(qsTr("Empty 1080p project at 30 fps"), "Ctrl+N")
                    ToolTip.delay: 600
                    background: Rectangle {
                        radius: StudioTheme.radius
                        color: newCard.hovered ? StudioTheme.raised : "transparent"
                        border.width: 1
                        border.color: newCard.hovered ? StudioTheme.accent : StudioTheme.border
                        Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                        Behavior on border.color { ColorAnimation { duration: StudioTheme.fast } }
                    }
                    contentItem: ColumnLayout {
                        spacing: 8
                        Item { Layout.fillHeight: true }
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 46
                            Layout.preferredHeight: 46
                            radius: 23
                            color: newCard.hovered ? StudioTheme.accent : StudioTheme.raised
                            Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                            StudioIcon { anchors.centerIn: parent; name: "plus"; size: 22; color: newCard.hovered ? "#FFFFFF" : StudioTheme.text }
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("New project")
                            color: StudioTheme.text
                            font.family: StudioTheme.font
                            font.pixelSize: StudioTheme.fontLarge
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                Repeater {
                    model: editor.recentProjects.slice(0, 11)
                    AbstractButton {
                        id: recentCard
                        readonly property var info: editor.projectInfo(modelData)
                        width: parent.cardW
                        height: width * 9 / 16 + 54
                        hoverEnabled: true
                        opacity: info.exists ? 1 : 0.5
                        onClicked: { if (editor.openPath(modelData)) start.done() }
                        ToolTip.visible: hovered
                        ToolTip.text: modelData
                        ToolTip.delay: 700
                        background: Rectangle {
                            radius: StudioTheme.radius
                            color: recentCard.hovered ? StudioTheme.raisedHover : StudioTheme.raised
                            border.width: 1
                            border.color: recentCard.hovered ? StudioTheme.alpha(StudioTheme.accent, 0.7) : StudioTheme.border
                            Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                            Behavior on border.color { ColorAnimation { duration: StudioTheme.fast } }
                        }
                        contentItem: ColumnLayout {
                            spacing: 0
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: recentCard.width * 9 / 16
                                Layout.margins: 1
                                radius: StudioTheme.radius - 1
                                color: StudioTheme.well
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: false
                                    source: recentCard.info.poster || ""
                                    scale: recentCard.hovered ? 1.04 : 1
                                    Behavior on scale { NumberAnimation { duration: StudioTheme.normal; easing.type: Easing.OutCubic } }
                                }
                                StudioIcon {
                                    anchors.centerIn: parent
                                    visible: !recentCard.info.poster
                                    name: "project"
                                    size: 34
                                    color: StudioTheme.textDisabled
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 10
                                Layout.rightMargin: 10
                                Layout.topMargin: 6
                                spacing: 1
                                Text {
                                    Layout.fillWidth: true
                                    text: recentCard.info.name
                                    color: StudioTheme.text
                                    font.family: StudioTheme.font
                                    font.pixelSize: StudioTheme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: recentCard.info.exists ? recentCard.info.modified + " · " + recentCard.info.folder : qsTr("Missing: %1").arg(recentCard.info.folder)
                                    color: StudioTheme.textSecondary
                                    font.family: StudioTheme.font
                                    font.pixelSize: StudioTheme.fontTiny
                                    elide: Text.ElideMiddle
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }

            // ---------------------------------------------------- templates
            SectionTitle { text: qsTr("Templates"); hint: qsTr("Sizes can change later in Project settings.") }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                readonly property int columns: Math.max(3, Math.min(6, Math.floor((content.width + spacing) / (180 + spacing))))
                readonly property real cardW: Math.floor((content.width - spacing * (columns - 1)) / columns)
                Repeater {
                    model: start.templateCards
                    AbstractButton {
                        id: tpl
                        width: parent.cardW
                        height: 168
                        hoverEnabled: true
                        onClicked: {
                            if (modelData.template)
                                editor.newFromTemplate(modelData.id)
                            else
                                editor.newProject(modelData.id)
                            start.done()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.description
                        ToolTip.delay: 600
                        background: Rectangle {
                            radius: StudioTheme.radius
                            color: tpl.hovered ? StudioTheme.raisedHover : StudioTheme.raised
                            border.width: 1
                            border.color: tpl.hovered ? StudioTheme.alpha(StudioTheme.accent, 0.7) : StudioTheme.border
                            Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                            Behavior on border.color { ColorAnimation { duration: StudioTheme.fast } }
                        }
                        contentItem: ColumnLayout {
                            spacing: 6
                            // the frame shape
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 96
                                Rectangle {
                                    anchors.centerIn: parent
                                    readonly property real s: Math.min(parent.width * 0.62 / modelData.w, 76 / modelData.h)
                                    width: modelData.w * s
                                    height: modelData.h * s
                                    radius: 5
                                    color: tpl.hovered ? StudioTheme.accentSoft : StudioTheme.well
                                    border.width: 1.5
                                    border.color: tpl.hovered ? StudioTheme.accent : StudioTheme.textDisabled
                                    Behavior on color { ColorAnimation { duration: StudioTheme.fast } }
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.shape
                                        color: tpl.hovered ? StudioTheme.accentText : StudioTheme.textSecondary
                                        font.family: StudioTheme.font
                                        font.pixelSize: StudioTheme.fontSmall
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                text: modelData.name
                                color: StudioTheme.text
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontBody
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                text: modelData.description
                                color: StudioTheme.textSecondary
                                font.family: StudioTheme.font
                                font.pixelSize: StudioTheme.fontTiny
                                elide: Text.ElideRight
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }

            // ---------------------------------------------------- empty sizes
            SectionTitle { text: qsTr("Empty project") }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [
                        { id: "1080p30", name: qsTr("1080p 30") }, { id: "1080p60", name: qsTr("1080p 60") }, { id: "1080p24", name: qsTr("1080p 24") },
                        { id: "4k30", name: qsTr("4K 30") }, { id: "4k60", name: qsTr("4K 60") }, { id: "720p30", name: qsTr("720p 30") },
                        { id: "vertical1080", name: qsTr("Vertical 9:16") }, { id: "square1080", name: qsTr("Square 1:1") }
                    ]
                    StudioButton {
                        text: modelData.name
                        flat: false
                        onClicked: { editor.newProject(modelData.id); start.done() }
                    }
                }
            }
            Item { Layout.preferredHeight: 24 }
        }
    }
}
