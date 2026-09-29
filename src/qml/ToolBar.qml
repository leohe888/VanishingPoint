import QtQuick 2.15
import VanishingPoint 1.0

pragma ComponentBehavior: Bound

Rectangle {
    id: root

    property int currentTool: Tools.CreatePlane
    required property VpController controller

    property color backgroundColor: "#535353"
    property color hoverColor: "#454545"
    property color selectedColor: "#363636"

    color: backgroundColor

    signal toolSelected(int toolId)


    readonly property var tools: [
        { toolId: Tools.EditPlane,   tooltip: qsTr("编辑平面工具 (V)"),  icon: "pointer.png",    shortcut: "V" },
        { toolId: Tools.CreatePlane, tooltip: qsTr("创建平面工具 (C)"),  icon: "perspective.png", shortcut: "C" },
        { toolId: Tools.Marquee,     tooltip: qsTr("选框工具 (M)"),     icon: "marquee.png",     shortcut: "M", separatorBefore: true },
        { toolId: Tools.CloneStamp,  tooltip: qsTr("图章工具 (S)"),     icon: "stamp.png",       shortcut: "S", separatorBefore: true },
        { toolId: Tools.Brush,       tooltip: qsTr("画笔工具 (B)"),     icon: "brush.png",       shortcut: "B" },
        { toolId: Tools.Transform,   tooltip: qsTr("变换工具 (T)"),     icon: "transform.png",   shortcut: "T", separatorBefore: true },
        { toolId: Tools.Hand, tooltip: qsTr("抓手工具 (H)"), icon: "hand.png", shortcut: "H", separatorBefore: true },
        { toolId: Tools.Zoom, tooltip: qsTr("缩放工具 (Z)"), icon: "magnifier.png", shortcut: "Z" }
    ]

    function selectTool(tool) {
        if (root.currentTool === tool)
            return
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

            delegate: Column {
                id: toolGroup
                required property var modelData
                width: 28
                spacing: 3

                Rectangle {
                    width: parent.width
                    height: 1
                    visible: toolGroup.modelData.separatorBefore === true
                    color: "#3E3E3E"
                }

                ToolButton {
                    enabled: (root.controller.availableTools & (1 << toolGroup.modelData.toolId)) !== 0
                    toolId: toolGroup.modelData.toolId
                    tooltip: toolGroup.modelData.tooltip
                    iconSource: toolGroup.modelData.icon
                    shortcut: toolGroup.modelData.shortcut
                    selected: root.currentTool === toolGroup.modelData.toolId
                    hoverColor: root.hoverColor
                    selectedColor: root.selectedColor
                    onActivated: (tool) => root.selectTool(tool)
                }
            }
        }
    }
}
