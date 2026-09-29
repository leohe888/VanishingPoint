import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

Window {
    width: 1280
    height: 820
    visible: true
    color: "#535353"
    title: qsTr("消失点")

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            id: toolBar
            z: 1    // 提升工具栏的层级，因为右侧内容区域后绘制，覆盖了工具栏向右扩展的 Tooltip
            Layout.preferredWidth: 38
            Layout.fillHeight: true
            currentTool: workArea.controller.tool
            onToolSelected: (toolId) => workArea.controller.tool = toolId
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
                    iconSource: "undo.png"
                    tooltip: qsTr("撤销")
                    tooltipBelow: true
                    enabled: workArea.controller.canUndo
                    onActivated: workArea.controller.undo()
                }
                ToolButton {
                    iconSource: "redo.png"
                    tooltip: qsTr("重做")
                    tooltipBelow: true
                    enabled: workArea.controller.canRedo
                    onActivated: workArea.controller.redo()
                }
                ToolOptionsBar {
                    controller: workArea.controller
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                }
            }

            HintBar {
                id: hintBar
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
        onAccepted: workArea.controller.openImage(selectedFile)
    }
    Shortcut { sequence: "Ctrl+O"; onActivated: imageDialog.open() }

    // 粘贴是窗口级快捷键：不必先点画布拿到焦点，也能直接粘贴。
    Shortcut {
        sequence: "Ctrl+V"
        onActivated: workArea.controller.pasteImage()
    }

    // 窗口级历史快捷键，不要求画布先获得焦点。
    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: workArea.controller.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]
        onActivated: workArea.controller.redo()
    }

    // 创建完平面等操作可能切换工具，工具栏随控制器状态同步。
    Connections {
        target: workArea.controller
        function onStatusMessage(text) { hintBar.message = text }
    }
}
