import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15

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

    ColumnLayout {
        id: contentColumn
        spacing: 4
        anchors {
            left: toolBar.right
            top: parent.top
            right: parent.right
            bottom: parent.bottom
        }

        ParameterBar {
            Layout.fillWidth: true
        }

        HintBar {
            Layout.fillWidth: true
        }

        WokArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
