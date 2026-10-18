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
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Automatic captions: engine status, style gallery with live previews,
// readability rules, generate with progress/cancel, and sidecar export.
Dialog {
    id: dialog
    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - 80 : 980, 1040)
    height: Math.min(Overlay.overlay ? Overlay.overlay.height - 60 : 720, 760)
    title: qsTr("Captions")
    padding: LingmoUI.Units.largeSpacing

    property string clipId: ""
    property var style: editor.captionTrackStyle
    readonly property var transcriber: editor.transcriber
    readonly property bool busy: editor.captionsBusy

    function openFor(id) {
        clipId = id || ""
        style = editor.captionTrackStyle
        gallery.selectedId = style.id
        open()
    }
    function set(key, value) {
        const s = Object.assign({}, style)
        s[key] = value
        style = s
        if (editor.hasCaptions)
            editor.setCaptionStyle(style)
    }
    function setRule(key, value) {
        const r = Object.assign({}, style.rules)
        r[key] = value
        set("rules", r)
    }

    FileDialog {
        id: exportDialog
        property string format: "srt"
        title: qsTr("Export captions")
        fileMode: FileDialog.SaveFile
        defaultSuffix: format
        nameFilters: format === "vtt" ? [qsTr("WebVTT (*.vtt)")] : format === "ass" ? [qsTr("Advanced SubStation (*.ass)")] : [qsTr("SubRip (*.srt)")]
        onAccepted: editor.exportCaptions(selectedFile, format)
    }

    contentItem: ColumnLayout {
        spacing: LingmoUI.Units.largeSpacing

        TranscriptionStatus { Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: LingmoUI.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: LingmoUI.Units.smallSpacing
                Label {
                    text: qsTr("Style")
                    font.pointSize: LingmoUI.Units.fontSubheading
                    font.bold: true
                    color: LingmoUI.Theme.textColor
                }
                CaptionStyleGallery {
                    id: gallery
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    vertical: editor.projectHeight > editor.projectWidth
                    animate: dialog.visible
                    selectedId: dialog.style.id
                    onChosen: function(s) {
                        // keep the safe area of the project/platform
                        s.safeTop = dialog.style.safeTop; s.safeBottom = dialog.style.safeBottom
                        s.safeLeft = dialog.style.safeLeft; s.safeRight = dialog.style.safeRight
                        dialog.style = s
                        if (editor.hasCaptions)
                            editor.setCaptionStyle(s)
                    }
                }
            }

            // ---- fine tuning
            LingmoUI.Card {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                ScrollView {
                    id: tuneScroll
                    anchors.fill: parent
                    anchors.margins: LingmoUI.Units.largeSpacing
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: tuneScroll.availableWidth
                        spacing: LingmoUI.Units.smallSpacing

                        Label { text: qsTr("Look"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor }
                        Label { text: qsTr("Position"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                        LingmoUI.SegmentedControl {
                            Layout.fillWidth: true
                            fillWidth: true
                            model: [qsTr("Bottom"), qsTr("Middle"), qsTr("Top")]
                            currentIndex: ["bottom", "middle", "top"].indexOf(dialog.style.position)
                            onActivated: function(i) { dialog.set("position", ["bottom", "middle", "top"][i]) }
                        }
                        Label { text: qsTr("Size: %1%").arg(Number(dialog.style.size || 6.5).toLocaleString(Qt.locale(), 'f', 1)); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                        Slider {
                            Layout.fillWidth: true
                            from: 3; to: 14; stepSize: 0.5
                            value: dialog.style.size || 6.5
                            onMoved: dialog.set("size", value)
                        }
                        Label { text: qsTr("Colors"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                        GridLayout {
                            columns: 2
                            columnSpacing: LingmoUI.Units.smallSpacing
                            rowSpacing: 2
                            Repeater {
                                model: [
                                    { key: "textColor", label: qsTr("Text") },
                                    { key: "activeColor", label: qsTr("Highlight") },
                                    { key: "outlineColor", label: qsTr("Outline") }
                                ]
                                RowLayout {
                                    id: swatchRow
                                    Layout.columnSpan: 2
                                    spacing: 4
                                    readonly property string swatchKey: modelData.key
                                    Label { text: modelData.label; Layout.preferredWidth: 72; font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.textColor }
                                    Repeater {
                                        // swatches that read well on video
                                        model: ["#ffffff", "#ffd60a", "#ffff00", "#30d158", "#5ac8fa", "#7c4dff", "#ff375f", "#000000"]
                                        Rectangle {
                                            id: swatch
                                            required property string modelData
                                            readonly property bool chosen: String(dialog.style[swatchRow.swatchKey]).toLowerCase() === modelData
                                            width: 20; height: 20; radius: 10
                                            color: modelData
                                            border.width: chosen ? 2 : 1
                                            border.color: chosen ? LingmoUI.Theme.highlightColor : LingmoUI.Theme.borderColor
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: dialog.set(swatchRow.swatchKey, swatch.modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Switch {
                            text: qsTr("Capital letters")
                            checked: dialog.style.uppercase === true
                            onToggled: dialog.set("uppercase", checked)
                        }

                        Label { text: qsTr("Readability"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor; Layout.topMargin: LingmoUI.Units.smallSpacing }
                        Label { text: qsTr("Lines per caption"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                        LingmoUI.SegmentedControl {
                            Layout.fillWidth: true
                            fillWidth: true
                            model: [qsTr("1 line"), qsTr("2 lines")]
                            currentIndex: (dialog.style.rules && dialog.style.rules.maxLines === 1) ? 0 : 1
                            onActivated: function(i) { dialog.setRule("maxLines", i + 1) }
                        }
                        RowLayout {
                            Label { text: qsTr("Characters per line"); Layout.fillWidth: true; font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                            SpinBox {
                                from: 0; to: 60
                                value: dialog.style.rules ? dialog.style.rules.maxCharsPerLine : 0
                                textFromValue: function(v) { return v === 0 ? qsTr("auto") : String(v) }
                                onValueModified: dialog.setRule("maxCharsPerLine", value)
                            }
                        }
                        RowLayout {
                            Label { text: qsTr("Characters per second"); Layout.fillWidth: true; font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                            SpinBox {
                                from: 8; to: 30
                                value: dialog.style.rules ? Math.round(dialog.style.rules.maxCps) : 17
                                onValueModified: dialog.setRule("maxCps", value)
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Lines break at phrase boundaries and never leave a single word alone. Captions stay at least %1 s on screen and avoid the platform's buttons.")
                                  .arg(Number((dialog.style.rules ? dialog.style.rules.minDurationMs : 833) / 1000).toLocaleString(Qt.locale(), 'f', 1))
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: LingmoUI.Theme.secondaryTextColor
                        }
                        Label { text: qsTr("Rules apply when captions are generated."); visible: editor.hasCaptions; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }

                        Label { text: qsTr("Export"); font.pointSize: LingmoUI.Units.fontBody; font.bold: true; color: LingmoUI.Theme.textColor; Layout.topMargin: LingmoUI.Units.smallSpacing }
                        Flow {
                            Layout.fillWidth: true
                            spacing: LingmoUI.Units.smallSpacing
                            Repeater {
                                model: ["srt", "vtt", "ass"]
                                LingmoUI.ActionButton {
                                    text: modelData.toUpperCase()
                                    enabled: editor.hasCaptions
                                    onClicked: { exportDialog.format = modelData; exportDialog.open() }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- progress
        ColumnLayout {
            Layout.fillWidth: true
            visible: dialog.busy
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: dialog.transcriber.stage
                font.pointSize: LingmoUI.Units.fontCaption
                color: LingmoUI.Theme.secondaryTextColor
                elide: Text.ElideRight
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0; to: 1
                value: Math.max(0, dialog.transcriber.progress)
                indeterminate: dialog.transcriber.progress < 0
            }
            Label {
                Layout.fillWidth: true
                visible: dialog.transcriber.running
                text: dialog.transcriber.timing
                font.pointSize: LingmoUI.Units.fontCaption
                color: LingmoUI.Theme.secondaryTextColor
                elide: Text.ElideRight
            }
        }
    }

    footer: RowLayout {
        spacing: LingmoUI.Units.smallSpacing
        Label {
            Layout.leftMargin: LingmoUI.Units.largeSpacing
            Layout.fillWidth: true
            text: editor.hasCaptions ? qsTr("Style changes apply to the captions on the timeline right away.") : qsTr("Captions are added for the selected clip, or the first clip with speech.")
            font.pointSize: LingmoUI.Units.fontCaption
            color: LingmoUI.Theme.secondaryTextColor
            elide: Text.ElideRight
        }
        LingmoUI.ActionButton {
            text: dialog.busy ? qsTr("Cancel") : qsTr("Close")
            Layout.bottomMargin: LingmoUI.Units.largeSpacing
            onClicked: dialog.busy ? editor.cancelCaptions() : dialog.close()
        }
        LingmoUI.ActionButton {
            text: editor.hasCaptions ? qsTr("Generate again") : qsTr("Generate captions")
            iconName: "media-record"
            primary: true
            busy: dialog.busy
            enabled: dialog.transcriber.available
            Layout.rightMargin: LingmoUI.Units.largeSpacing
            Layout.bottomMargin: LingmoUI.Units.largeSpacing
            onClicked: {
                editor.generateCaptions(dialog.clipId, dialog.style.id)
            }
        }
    }

    Connections {
        target: editor
        function onCaptionsBusyChanged() {
            // once generated, the chosen tweaks apply to the new track
            if (!editor.captionsBusy && editor.hasCaptions)
                editor.setCaptionStyle(dialog.style)
        }
    }
}
