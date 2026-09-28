import QtQuick 2.15
import VanishingPoint 1.0

pragma ComponentBehavior: Bound

Rectangle {
    id: root

    property int currentTool: VpController.CreatePlane

    property color backgroundColor: "#535353"
    property color hoverColor: "#454545"
    property color selectedColor: "#363636"

    color: backgroundColor

    signal toolSelected(int toolId)

    Component.onCompleted: root.toolSelected(root.currentTool)

    readonly property var tools: [
        { toolId: VpController.EditPlane,   tooltip: qsTr("编辑平面工具 (V)"),  icon: "pointer.png",    shortcut: "V" },
        { toolId: VpController.CreatePlane, tooltip: qsTr("创建平面工具 (C)"),  icon: "perspective.png", shortcut: "C" },
        { toolId: VpController.Marquee,     tooltip: qsTr("选框工具 (M)"),     icon: "marquee.png",     shortcut: "M" },
        { toolId: VpController.CloneStamp,  tooltip: qsTr("图章工具 (S)"),     icon: "stamp.png",       shortcut: "S" },
        { toolId: VpController.Brush,       tooltip: qsTr("画笔工具 (B)"),     icon: "brush.png",       shortcut: "B" },
        { toolId: VpController.Transform,   tooltip: qsTr("变换工具 (T)"),     icon: "transform.png",   shortcut: "T" }
    ]

    function selectTool(tool) {
        if (root.currentTool === tool)
            return
        root.currentTool = tool
        root.toolSelected(tool)
    }

    Column {
        id: toolColumn
        anchors {
            top: parent.top
            topMargin: 7
            horizontalCenter: parent.horizontalCenter
        }
        spacing: 3

        Repeater {
            model: root.tools

            delegate: ToolButton {
                required property var modelData
                toolId: modelData.toolId
                tooltip: modelData.tooltip
                iconSource: modelData.icon
                shortcut: modelData.shortcut
                selected: root.currentTool === modelData.toolId
                hoverColor: root.hoverColor
                selectedColor: root.selectedColor
                onActivated: (tool) => root.selectTool(tool)
            }
        }
    }
}
