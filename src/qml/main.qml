import QtQuick 2.15
import VanishingPoint 1.0
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

Window {
    id: window
    width: 1280
    height: 820
    visible: true
    color: VpTheme.panelBackground
    title: qsTr("消失点")

    function requestSave(saveAs) {
        if (!vpController.canSave)
            return
        if (!saveAs && vpController.savedImageUrl.toString().length > 0) {
            vpController.saveImage(vpController.savedImageUrl)
            return
        }
        saveDialog.selectedFile = vpController.suggestedSaveUrl()
        saveDialog.open()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            id: toolBar
            objectName: "toolBar"
            z: 1    // 提升工具栏的层级，因为子项默认按声明顺序堆叠，后面的显示在上面，覆盖了工具栏向右扩展的 Tooltip
            Layout.preferredWidth: 38
            Layout.fillHeight: true
            currentTool: workArea.canvas.temporaryHandActive ? VpTools.Hand : vpController.tool
            onToolSelected: (toolId) => vpController.tool = toolId
        }

        ColumnLayout {
            id: mainColumn
            spacing: 4
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                Layout.fillWidth: true
                z: 1
                ToolButton {
                    iconSource: "folder-open.png"
                    tooltip: qsTr("打开图片")
                    tooltipBelow: true
                    onActivated: imageDialog.open()
                }
                ToolButton {
                    objectName: "saveButton"
                    iconSource: "image_save.svg"
                    tooltip: qsTr("保存图片 (Ctrl+S)")
                    tooltipBelow: true
                    enabled: vpController.canSave
                    onActivated: window.requestSave(false)
                }
                ToolButton {
                    iconSource: "undo.png"
                    tooltip: qsTr("撤销")
                    tooltipBelow: true
                    enabled: vpController.canUndo
                    onActivated: vpController.undo()
                }
                ToolButton {
                    iconSource: "redo.png"
                    tooltip: qsTr("重做")
                    tooltipBelow: true
                    enabled: vpController.canRedo
                    onActivated: vpController.redo()
                }
                ToolOptionsBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                }
            }

            HintBar {
                id: hintBar
                controller: vpController
                Layout.fillWidth: true
                Layout.preferredHeight: 38
            }

            WorkArea {
                id: workArea
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    FileDialog {
        id: imageDialog
        title: qsTr("打开图片")
        nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"), qsTr("所有文件 (*)")]
        onAccepted: vpController.openImage(selectedFile)
    }

    FileDialog {
        id: saveDialog
        objectName: "saveDialog"
        title: qsTr("保存图片")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG 图片 (*.png)"), qsTr("JPEG 图片 (*.jpg *.jpeg)"), qsTr("BMP 图片 (*.bmp)")]
        defaultSuffix: selectedNameFilter.index === 1 ? "jpg"
                       : selectedNameFilter.index === 2 ? "bmp" : "png"
        onAccepted: vpController.saveImage(selectedFile)
    }

    Shortcut {
        objectName: "saveShortcut"
        sequence: "Ctrl+S"
        enabled: vpController.canSave
        onActivated: window.requestSave(false)
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        enabled: vpController.canSave
        onActivated: window.requestSave(true)
    }

    Shortcut {
        sequence: "Ctrl+O";
        onActivated: imageDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+C"
        onActivated: vpController.copySelection()
    }

    Shortcut {
        sequence: "Ctrl+V"
        onActivated: vpController.pasteSelection()
    }

    Shortcut {
        sequence: "Ctrl+D"
        onActivated: vpController.clearSelection()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: vpController.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]
        onActivated: vpController.redo()
    }

}
