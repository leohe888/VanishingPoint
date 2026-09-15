import QtQuick 2.15

pragma ComponentBehavior: Bound

Rectangle {
    id: root

    property string currentTool: "create"

    property color panelColor: "#535353"
    property color hoverColor: "#454545"
    property color selectedColor: "#363636"

    width: 38
    color: panelColor

    signal toolSelected(string tool)

    readonly property var tools: [
        { toolName: "edit",         tooltip: qsTr("编辑平面工具 (V)"),  icon: "pointer.png",    shortcut: "V" },
        { toolName: "create",       tooltip: qsTr("创建平面工具 (C)"),  icon: "perspective.png",shortcut: "C" },
        { toolName: "marquee",      tooltip: qsTr("选框工具 (M)"),     icon: "marquee.png",     shortcut: "M" },
        { toolName: "stamp",        tooltip: qsTr("图章工具 (S)"),     icon: "stamp.png",       shortcut: "S" },
        { toolName: "brush",        tooltip: qsTr("画笔工具 (B)"),     icon: "brush.png",       shortcut: "B" },
        { toolName: "transform",    tooltip: qsTr("变换工具 (T)"),     icon: "transform.png",   shortcut: "T" }
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
                toolName: modelData.toolName
                tooltip: modelData.tooltip
                iconSource: modelData.icon
                shortcut: modelData.shortcut
                selected: root.currentTool === modelData.toolName
                hoverColor: root.hoverColor
                selectedColor: root.selectedColor
                onActivated: (tool) => root.selectTool(tool)
            }
        }
    }
}
