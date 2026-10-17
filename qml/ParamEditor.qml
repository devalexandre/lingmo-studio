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

// Editor for one effect parameter, chosen by its type from the registry.
ColumnLayout {
    id: editorRow

    property var def: ({})
    property var value
    property bool animated: false
    signal edited(var value)

    spacing: 2

    RowLayout {
        Layout.fillWidth: true
        visible: editorRow.def.type !== "bool"
        Label {
            text: editorRow.def.name || ""
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontCaption
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Rectangle {
            visible: editorRow.animated && editorRow.def.animatable === true
            width: 7
            height: 7
            rotation: 45
            color: LingmoUI.Theme.orangeColor
        }
        TextField {
            visible: editorRow.def.type === "double" || editorRow.def.type === "int"
            Layout.preferredWidth: 72
            horizontalAlignment: Text.AlignRight
            font.pointSize: LingmoUI.Units.fontCaption
            font.features: { "tnum": 1 }
            text: visible ? Number(editorRow.value).toLocaleString(Qt.locale(), 'f', editorRow.def.step < 0.1 ? 2 : (editorRow.def.step < 1 ? 1 : 0)) + (editorRow.def.unit ? " " + editorRow.def.unit : "") : ""
            selectByMouse: true
            onEditingFinished: {
                const v = Number.fromLocaleString(Qt.locale(), text.replace(/[^0-9,.\-]/g, ""))
                if (!isNaN(v))
                    editorRow.edited(Math.max(editorRow.def.min, Math.min(editorRow.def.max, v)))
            }
        }
    }

    Loader {
        Layout.fillWidth: true
        sourceComponent: {
            switch (editorRow.def.type) {
            case "double": case "int": return sliderEditor
            case "color": return colorEditor
            case "text": return textEditor
            case "font": return fontEditor
            case "choice": return choiceEditor
            case "file": return fileEditor
            case "bool": return boolEditor
            case "curve": return curveEditor
            }
            return null
        }
    }

    Component {
        id: sliderEditor
        Slider {
            from: editorRow.def.min
            to: editorRow.def.max
            stepSize: editorRow.def.step
            value: Number(editorRow.value)
            onMoved: editorRow.edited(value)
            // double-click resets to the default
            TapHandler { onDoubleTapped: editorRow.edited(editorRow.def.def) }
        }
    }
    Component {
        id: colorEditor
        RowLayout {
            Rectangle {
                Layout.preferredWidth: 46
                Layout.preferredHeight: LingmoUI.Units.smallControlHeight
                radius: LingmoUI.Theme.tinyRadius
                color: editorRow.value || "transparent"
                border.color: LingmoUI.Theme.borderColor
                MouseArea { anchors.fill: parent; onClicked: colorDialog.open() }
            }
            Label {
                text: String(editorRow.value)
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
            ColorDialog {
                id: colorDialog
                selectedColor: editorRow.value
                options: ColorDialog.ShowAlphaChannel
                onAccepted: editorRow.edited(String(selectedColor))
            }
        }
    }
    Component {
        id: textEditor
        TextArea {
            text: editorRow.value || ""
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            font.pointSize: LingmoUI.Units.fontBody
            Timer {
                id: commit
                interval: 500
                onTriggered: if (parent.text !== editorRow.value) editorRow.edited(parent.text)
            }
            onTextChanged: if (activeFocus) commit.restart()
        }
    }
    Component {
        id: fontEditor
        ComboBox {
            model: Qt.fontFamilies()
            currentIndex: model.indexOf(editorRow.value)
            displayText: editorRow.value || ""
            onActivated: editorRow.edited(currentText)
        }
    }
    Component {
        id: choiceEditor
        ComboBox {
            model: editorRow.def.choices
            currentIndex: Math.max(0, (editorRow.def.choices || []).indexOf(editorRow.value))
            onActivated: editorRow.edited(currentText)
        }
    }
    Component {
        id: fileEditor
        RowLayout {
            TextField {
                Layout.fillWidth: true
                text: editorRow.value || ""
                placeholderText: qsTr("No file")
                readOnly: true
            }
            LingmoUI.IconButton {
                iconName: "document-open"
                tipText: qsTr("Choose…")
                onClicked: fileDialog.open()
            }
            LingmoUI.IconButton {
                iconName: "edit-clear"
                tipText: qsTr("Clear")
                visible: (editorRow.value || "").length > 0
                onClicked: editorRow.edited("")
            }
            FileDialog {
                id: fileDialog
                nameFilters: editorRow.def.id === "file" ? [qsTr("3D LUT (*.cube *.3dl)"), qsTr("All files (*)")] : [qsTr("All files (*)")]
                onAccepted: editorRow.edited(editor.localPath(selectedFile))
            }
        }
    }
    Component {
        id: boolEditor
        Switch {
            text: editorRow.def.name
            checked: editorRow.value === true
            onToggled: editorRow.edited(checked)
        }
    }
    Component {
        id: curveEditor
        TextField {
            text: editorRow.value || ""
            font.pointSize: LingmoUI.Units.fontCaption
            onEditingFinished: editorRow.edited(text)
        }
    }
}
