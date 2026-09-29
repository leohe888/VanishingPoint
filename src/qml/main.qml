import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15

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
            onToolSelected: (toolId) => workArea.controller.tool = toolId
        }

        ColumnLayout {
            id: mainColumn
            spacing: 4
            Layout.fillWidth: true
            Layout.fillHeight: true

            ToolOptionsBar {
                controller: workArea.controller
                Layout.fillWidth: true
                Layout.preferredHeight: 38
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

    // 粘贴是窗口级快捷键：不必先点画布拿到焦点，也能直接粘贴。
    Shortcut {
        sequence: "Ctrl+V"
        onActivated: workArea.controller.pasteImage()
    }

    // 窗口级历史快捷键，不要求画布先获得焦点。
    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: workArea.canvas.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]
        onActivated: workArea.canvas.redo()
    }

    // 创建完平面等操作可能切换工具，工具栏随控制器状态同步。
    Connections {
        target: workArea.controller
        function onToolChanged() { toolBar.selectTool(workArea.controller.tool) }
        function onStatusMessage(text) { hintBar.message = text }
    }
}
