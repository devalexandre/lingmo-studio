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

// Deliver page: export presets, hardware encoding, render queue.
RowLayout {
    id: page
    spacing: LingmoUI.Units.smallSpacing

    property string presetId: "mp4-h264"
    readonly property var preset: {
        const list = editor.exportPresets
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === presetId)
                return list[i]
        return list[0]
    }
    property string output: ""
    onPresetChanged: output = editor.suggestedExportPath(presetId)
    Component.onCompleted: output = editor.suggestedExportPath(presetId)
    Connections {
        target: editor
        function onProjectChanged() { page.output = editor.suggestedExportPath(page.presetId) }
    }
    readonly property string hwFor: {
        if (!preset || ["h264", "hevc", "av1"].indexOf(preset.codec) < 0)
            return ""
        const nv = preset.codec + "_nvenc", va = preset.codec + "_vaapi"
        return editor.hwEncoders.indexOf(nv) >= 0 ? nv : (editor.hwEncoders.indexOf(va) >= 0 ? va : "")
    }
    readonly property bool projectVertical: editor.projectHeight > editor.projectWidth

    FileDialog {
        id: outputDialog
        title: qsTr("Export to")
        fileMode: FileDialog.SaveFile
        currentFile: "file://" + page.output
        defaultSuffix: page.preset ? page.preset.extension : "mp4"
        onAccepted: page.output = editor.localPath(selectedFile)
    }

    StudioPanel {
        Layout.fillHeight: true
        Layout.preferredWidth: 520
        title: qsTr("Format")

        GridView {
            id: grid
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.smallSpacing
            cellWidth: width / 2
            cellHeight: 76
            clip: true
            model: editor.exportPresets
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight
                LingmoUI.Card {
                    anchors.fill: parent
                    anchors.margins: 3
                    clickable: true
                    highlighted: modelData.id === page.presetId
                    onClicked: page.presetId = modelData.id
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: LingmoUI.Units.largeSpacing
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: modelData.name
                            font.pointSize: LingmoUI.Units.fontBody
                            font.bold: true
                            color: LingmoUI.Theme.textColor
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "." + modelData.extension + " · " + (modelData.audioOnly ? qsTr("audio") : (modelData.width > 0 ? (modelData.width + "×" + (modelData.height > 0 ? modelData.height : "…")) : qsTr("project size")))
                            font.pointSize: LingmoUI.Units.fontCaption
                            color: LingmoUI.Theme.secondaryTextColor
                        }
                    }
                }
            }
        }
    }

    StudioPanel {
        Layout.fillHeight: true
        Layout.preferredWidth: 420
        title: qsTr("Settings")

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.largeSpacing
            spacing: LingmoUI.Units.largeSpacing

            LingmoUI.Banner {
                Layout.fillWidth: true
                type: LingmoUI.Banner.Warning
                closable: false
                visible: page.preset && page.preset.vertical && !page.projectVertical
                title: visible ? qsTr("This is a horizontal project") : ""
                text: visible ? qsTr("Reframe it for 9:16 first, or the video gets black bars.") : ""
                LingmoUI.ActionButton { text: qsTr("Reframe to 9:16"); onClicked: editor.smartReframe("9:16") }
            }

            Label { text: qsTr("File"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    text: page.output
                    onEditingFinished: page.output = text
                }
                LingmoUI.IconButton { iconName: "document-open"; tipText: qsTr("Choose…"); onClicked: outputDialog.open() }
            }

            Switch {
                id: hw
                text: page.hwFor.length > 0 ? qsTr("Use the graphics card (%1)").arg(page.hwFor) : qsTr("Use the graphics card")
                checked: true
                enabled: page.hwFor.length > 0
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: !editor.hwDetected ? qsTr("Checking which encoders the graphics card offers…")
                      : page.hwFor.length > 0 ? qsTr("Faster export; software encoding is used if it fails to start.")
                      : qsTr("No working hardware encoder for this format; the CPU encodes it.")
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: page.preset && !page.preset.audioOnly && page.preset.codec !== "gif" && page.preset.codec !== "prores" && page.preset.codec !== "dnxhr"
                Label { text: qsTr("Quality"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                RowLayout {
                    Label { text: qsTr("Smaller"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                    Slider {
                        id: quality
                        Layout.fillWidth: true
                        from: 34; to: 14
                        stepSize: 1
                        value: 21
                    }
                    Label { text: qsTr("Better"); font.pointSize: LingmoUI.Units.fontCaption; color: LingmoUI.Theme.secondaryTextColor }
                }
            }

            LingmoUI.SegmentedControl {
                id: range
                model: [qsTr("Whole timeline"), qsTr("In to out")]
                enabled: editor.player.markIn >= 0
            }

            Item { Layout.fillHeight: true }

            LingmoUI.ActionButton {
                Layout.fillWidth: true
                text: qsTr("Add to render queue")
                iconName: "document-export"
                primary: true
                enabled: editor.timeline.duration > 1 && page.output.length > 0
                onClicked: editor.queueExport(page.presetId, "file://" + page.output, hw.checked && page.hwFor.length > 0,
                                              (page.preset.codec === "h264" || page.preset.codec === "hevc" || page.preset.codec === "av1" || page.preset.codec === "vp9") ? Math.round(quality.value) : -1,
                                              range.currentIndex === 1)
            }
            LingmoUI.ActionButton {
                Layout.fillWidth: true
                text: qsTr("Export subtitles (SRT)…")
                onClicked: srtSave.open()
            }
            FileDialog {
                id: srtSave
                fileMode: FileDialog.SaveFile
                defaultSuffix: "srt"
                nameFilters: [qsTr("Subtitles (*.srt)")]
                onAccepted: editor.exportSubtitles(selectedFile)
            }
        }
    }

    StudioPanel {
        Layout.fillHeight: true
        Layout.fillWidth: true
        title: qsTr("Render queue")
        actions: [
            LingmoUI.ActionButton { text: qsTr("Clear finished"); subtle: true; enabled: editor.renderQueue.count > 0; onClicked: editor.renderQueue.clearFinished() }
        ]

        ListView {
            id: queue
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.smallSpacing
            spacing: LingmoUI.Units.smallSpacing
            clip: true
            model: editor.renderQueue
            ScrollBar.vertical: ScrollBar {}
            delegate: LingmoUI.Card {
                id: job
                required property int index
                required property string name
                required property string output
                required property string preset
                required property string state
                required property int progress
                required property string encoder
                required property string message
                required property int eta
                width: queue.width
                implicitHeight: jobCol.implicitHeight + LingmoUI.Units.largeSpacing * 2
                ColumnLayout {
                    id: jobCol
                    anchors.fill: parent
                    anchors.margins: LingmoUI.Units.largeSpacing
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: job.name
                            font.pointSize: LingmoUI.Units.fontBody
                            font.bold: true
                            color: LingmoUI.Theme.textColor
                            elide: Text.ElideMiddle
                        }
                        LingmoUI.Badge {
                            text: ({ queued: qsTr("queued"), running: job.progress + "%", done: qsTr("done"), failed: qsTr("failed"), cancelled: qsTr("cancelled") })[job.state]
                            tone: job.state === "done" ? LingmoUI.Theme.greenColor : job.state === "failed" ? LingmoUI.Theme.redColor : LingmoUI.Theme.highlightColor
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: job.preset + (job.encoder ? " · " + job.encoder : "") + (job.state === "running" && job.eta >= 0 ? " · " + qsTr("%1 s left").arg(job.eta) : "")
                        color: LingmoUI.Theme.secondaryTextColor
                        font.pointSize: LingmoUI.Units.fontCaption
                        elide: Text.ElideRight
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        visible: job.state === "running" || job.state === "queued"
                        from: 0; to: 100
                        value: job.progress
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: job.message.length > 0
                        text: job.message
                        wrapMode: Text.WordWrap
                        color: LingmoUI.Theme.errorColor
                        font.pointSize: LingmoUI.Units.fontCaption
                    }
                    RowLayout {
                        Item { Layout.fillWidth: true }
                        LingmoUI.ActionButton {
                            visible: job.state === "running" || job.state === "queued"
                            text: qsTr("Cancel")
                            destructive: true
                            subtle: true
                            onClicked: editor.renderQueue.cancel(job.index)
                        }
                        LingmoUI.ActionButton {
                            visible: job.state === "done"
                            text: qsTr("Show in folder")
                            onClicked: editor.renderQueue.openFolder(job.index)
                        }
                    }
                }
            }
        }
        LingmoUI.EmptyState {
            anchors.centerIn: parent
            width: parent.width - LingmoUI.Units.largeSpacing * 2
            visible: queue.count === 0
            iconName: "document-export"
            iconSize: LingmoUI.Units.iconSizes.large
            title: qsTr("Nothing rendering")
            text: qsTr("Pick a format and add the project to the queue. You can keep editing while it renders.")
        }
    }
}
