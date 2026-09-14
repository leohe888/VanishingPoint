import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    width: 1280
    height: 820
    visible: true
    color: "#535353"
    title: qsTr("消失点")

    ToolBar {
        id: toolBar
        z: 1    // 提升工具栏的层级，因为右侧内容区域后绘制，覆盖了工具栏向右扩展的 Tooltip
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        onToolSelected: (tool) => console.log("Selected tool:", tool)
    }

    Column {
        id: con
        spacing: 4
        anchors {
            left: toolBar.right
            top: parent.top
            right: parent.right
            bottom: parent.bottom
        }

        Rectangle {
            id: parameterBar
            width: parent.width
            height: 38
            color: "#535353"
        }

        Rectangle {
            id: hintBar
            width: parent.width
            height: 38
            color: "#535353"
            border.color: "#3E3E3E"
            border.width: 1
        }

        Rectangle {
            id: canvas
            width: parent.width
            height: Math.max(0, parent.height
                             - parameterBar.height
                             - hintBar.height
                             - editorLayout.spacing * 2)
            color: "#4D4D4D"
            border.color: "#3E3E3E"
            border.width: 1
        }
    }
}
