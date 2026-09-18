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
            onToolSelected: (toolId) => workArea.canvas.tool = toolId
        }

        ColumnLayout {
            id: mainColumn
            spacing: 4
            Layout.fillWidth: true
            Layout.fillHeight: true

            ToolOptionsBar {
                canvas: workArea.canvas
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

    // 画布可能自行切换工具（例如创建完平面后进入编辑工具），工具栏需跟随同步。
    Connections {
        target: workArea.canvas
        function onToolChanged() { toolBar.selectTool(workArea.canvas.tool) }
        function onStatusMessage(text) { hintBar.message = text }
    }
}
