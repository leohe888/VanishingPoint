import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    width: 960
    height: 640
    visible: true
    color: "#a9aaa0"
    title: qsTr("消失点")

    ToolBar {
        id: toolBar
        z: 1    // 提示工具栏的层级，因为右侧内容区域后绘制，覆盖了工具栏向右扩展的 Tooltip
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        onToolSelected: (tool) => console.log("Selected tool:", tool)
    }

    Rectangle {
        anchors {
            left: toolBar.right
            top: parent.top
            right: parent.right
            bottom: parent.bottom
        }
        color: "#a9aaa0"
    }
}
