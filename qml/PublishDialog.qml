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
import QtQuick.Dialogs
import QtQuick.Layouts

// Export › title, description, tags and thumbnail for the video, written by the
// local AI from what is said in it. Every field stays editable; the thumbnail
// is recomposed as the headline, style or frame change.
Dialog {
    id: dialog
    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - 80 : 1040, 1080)
    height: Math.min(Overlay.overlay ? Overlay.overlay.height - 60 : 720, 740)
    title: qsTr("Title, description and thumbnail")
    padding: 14

    readonly property var gen: editor.metadata
    readonly property bool vertical: gen.thumbnailSize.height > gen.thumbnailSize.width

    FileDialog {
        id: thumbDialog
        title: qsTr("Save thumbnail")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "jpg"
        currentFile: "file://" + dialog.gen.suggestedThumbnailPath()
        nameFilters: [qsTr("Images (*.jpg *.png)")]
        onAccepted: dialog.gen.saveThumbnail(selectedFile)
    }
    FileDialog {
        id: textDialog
        title: qsTr("Save title and description")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        currentFile: "file://" + dialog.gen.suggestedTextPath()
        nameFilters: [qsTr("Text (*.txt)")]
        onAccepted: dialog.gen.saveTexts(selectedFile)
    }

    component FieldLabel: RowLayout {
        property alias text: label.text
        property string copyText: ""
        signal copyRequested(string text)
        Layout.fillWidth: true
        Text {
            id: label
            Layout.fillWidth: true
            color: StudioTheme.textSecondary
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontSmall
            font.weight: Font.DemiBold
        }
        StudioButton {
            visible: copyText.length > 0
            text: qsTr("Copy")
            size: 22
            fontSize: StudioTheme.fontSmall
            onClicked: copyRequested(copyText)
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

        // ---- progress and notices
        ColumnLayout {
            Layout.fillWidth: true
            visible: dialog.gen.running
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: dialog.gen.stage
                elide: Text.ElideRight
                color: StudioTheme.textSecondary
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontSmall
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0; to: 1
                value: Math.max(0, dialog.gen.progress)
                indeterminate: dialog.gen.progress < 0
            }
        }
        Rectangle {
            Layout.fillWidth: true
            visible: !dialog.gen.running && (dialog.gen.error.length > 0 || dialog.gen.notice.length > 0)
            implicitHeight: noticeText.implicitHeight + 16
            radius: StudioTheme.radius
            color: StudioTheme.alpha(dialog.gen.error.length > 0 ? StudioTheme.danger : StudioTheme.warning, 0.12)
            border.width: 1
            border.color: StudioTheme.alpha(dialog.gen.error.length > 0 ? StudioTheme.danger : StudioTheme.warning, 0.5)
            Text {
                id: noticeText
                anchors.fill: parent
                anchors.margins: 8
                wrapMode: Text.WordWrap
                text: dialog.gen.error.length > 0 ? dialog.gen.error : dialog.gen.notice
                color: StudioTheme.text
                font.family: StudioTheme.font
                font.pixelSize: StudioTheme.fontSmall
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // ---------------------------------------------------- texts
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 6

                FieldLabel {
                    text: qsTr("Title (%1/100)").arg(titleField.text.length)
                    copyText: titleField.text
                    onCopyRequested: function(t) { dialog.gen.copyToClipboard(t) }
                }
                TextField {
                    id: titleField
                    Layout.fillWidth: true
                    enabled: dialog.gen.hasResult
                    placeholderText: qsTr("Generate to get a title")
                    maximumLength: 100
                    text: dialog.gen.title
                    onTextChanged: if (text !== dialog.gen.title) dialog.gen.title = text
                }

                FieldLabel {
                    text: qsTr("Description")
                    copyText: descriptionField.text
                    onCopyRequested: function(t) { dialog.gen.copyToClipboard(t) }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        id: descriptionField
                        enabled: dialog.gen.hasResult
                        wrapMode: TextEdit.Wrap
                        placeholderText: qsTr("Generate to get a description")
                        text: dialog.gen.description
                        onTextChanged: if (text !== dialog.gen.description) dialog.gen.description = text
                    }
                }

                FieldLabel {
                    text: qsTr("Tags")
                    copyText: tagsField.text
                    onCopyRequested: function(t) { dialog.gen.copyToClipboard(t) }
                }
                TextField {
                    id: tagsField
                    Layout.fillWidth: true
                    enabled: dialog.gen.hasResult
                    placeholderText: qsTr("keyword, keyword, …")
                    text: dialog.gen.tags
                    onTextChanged: if (text !== dialog.gen.tags) dialog.gen.tags = text
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: dialog.gen.hasResult
                    text: dialog.gen.usedLlm ? qsTr("Written by %1 from the video's speech. Check it before publishing.").arg(dialog.gen.llmModel)
                                             : qsTr("A simple version made from the transcript. Choose an Ollama model in Settings › AI for better text.")
                    color: StudioTheme.textSecondary
                    font.family: StudioTheme.font
                    font.pixelSize: StudioTheme.fontTiny
                }
            }

            // ---------------------------------------------------- thumbnail
            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: 440
                Layout.maximumWidth: 440
                spacing: 6

                FieldLabel { text: qsTr("Thumbnail") }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 180
                    radius: StudioTheme.radius
                    color: StudioTheme.well
                    border.width: 1
                    border.color: StudioTheme.border
                    clip: true
                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        source: dialog.gen.thumbnail
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        smooth: true
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: dialog.gen.thumbnail.length === 0
                        text: dialog.gen.running ? qsTr("Choosing a frame…") : qsTr("The thumbnail appears here")
                        color: StudioTheme.textDisabled
                        font.family: StudioTheme.font
                        font.pixelSize: StudioTheme.fontBody
                    }
                }

                FieldLabel { text: qsTr("Headline on the image") }
                TextField {
                    Layout.fillWidth: true
                    enabled: dialog.gen.frames.length > 0
                    maximumLength: 40
                    placeholderText: qsTr("2 to 4 words")
                    text: dialog.gen.thumbnailText
                    onTextChanged: if (text !== dialog.gen.thumbnailText) dialog.gen.thumbnailText = text
                }

                Row {
                    spacing: 2
                    Repeater {
                        model: dialog.gen.styles
                        StudioButton {
                            required property var modelData
                            text: modelData.name
                            flat: false
                            checkable: true
                            checked: dialog.gen.thumbnailStyle === modelData.id
                            enabled: dialog.gen.frames.length > 0
                            onClicked: dialog.gen.thumbnailStyle = modelData.id
                        }
                    }
                }

                FieldLabel { text: qsTr("Frame"); visible: dialog.gen.frames.length > 0 }
                Row {
                    visible: dialog.gen.frames.length > 0
                    spacing: 6
                    Repeater {
                        model: dialog.gen.frames
                        Rectangle {
                            id: frameBox
                            required property int index
                            required property var modelData
                            readonly property bool current: index === dialog.gen.selectedFrame
                            width: 66
                            height: dialog.vertical ? 100 : 40
                            radius: StudioTheme.smallRadius
                            color: StudioTheme.well
                            border.width: current ? 2 : 1
                            border.color: current ? StudioTheme.accent : StudioTheme.border
                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                source: frameBox.modelData.url
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: false
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: dialog.gen.selectedFrame = frameBox.index
                                ToolTip.visible: containsMouse
                                ToolTip.text: frameBox.modelData.time
                                ToolTip.delay: 300
                            }
                        }
                    }
                }
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Text {
            Layout.leftMargin: 14
            Layout.fillWidth: true
            text: qsTr("Uses the speech of the timeline and the AI model chosen in Settings › AI. Nothing is uploaded.")
            elide: Text.ElideRight
            color: StudioTheme.textSecondary
            font.family: StudioTheme.font
            font.pixelSize: StudioTheme.fontSmall
        }
        StudioButton {
            text: qsTr("Save thumbnail…")
            iconName: "image"
            flat: false
            Layout.bottomMargin: 12
            enabled: dialog.gen.thumbnail.length > 0 && !dialog.gen.running
            onClicked: thumbDialog.open()
        }
        StudioButton {
            text: qsTr("Save text…")
            iconName: "save"
            flat: false
            Layout.bottomMargin: 12
            enabled: dialog.gen.hasResult && !dialog.gen.running
            onClicked: textDialog.open()
        }
        StudioButton {
            text: dialog.gen.running ? qsTr("Cancel") : qsTr("Close")
            flat: false
            Layout.bottomMargin: 12
            onClicked: dialog.gen.running ? dialog.gen.cancel() : dialog.close()
        }
        StudioButton {
            text: dialog.gen.hasResult ? qsTr("Generate again") : qsTr("Generate")
            iconName: "reset"
            primary: true
            Layout.rightMargin: 14
            Layout.bottomMargin: 12
            enabled: !dialog.gen.running && editor.timeline.duration > 1
            onClicked: dialog.gen.generate()
        }
    }
}
