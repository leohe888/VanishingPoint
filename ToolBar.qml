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
        { name: "edit",         label: qsTr("编辑平面工具 (V)"),  icon: "pointer.png",    shortcut: "V" },
        { name: "create",       label: qsTr("创建平面工具 (C)"),  icon: "perspective.png",shortcut: "C" },
        { name: "marquee",      label: qsTr("选框工具 (M)"),     icon: "marquee.png",     shortcut: "M" },
        { name: "stamp",        label: qsTr("图章工具 (S)"),     icon: "stamp.png",       shortcut: "S" },
        { name: "brush",        label: qsTr("画笔工具 (B)"),     icon: "brush.png",       shortcut: "B" },
        { name: "transform",    label: qsTr("变换工具 (T)"),     icon: "transform.png",   shortcut: "T" }
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

            delegate: Rectangle {
                id: button
                required property var modelData
                width: 28
                height: 28
                radius: 2
                color: root.currentTool === modelData.name
                       ? root.selectedColor
                       : (mouseArea.containsMouse ? root.hoverColor : "transparent")
                border.width: (root.currentTool === modelData.name || mouseArea.containsMouse) ? 1 : 0
                border.color: "#777777"

                Image {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    source: "qrc:/assets/icons/" + button.modelData.icon
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    opacity: root.currentTool === button.modelData.name ? 1.0 : 0.82
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selectTool(button.modelData.name)

                    ToolTip {
                        visible: mouseArea.containsMouse
                        text: button.modelData.label
                    }
                }

                Shortcut {
                    sequence: button.modelData.shortcut
                    onActivated: root.selectTool(button.modelData.name)
                }
            }
        }
    }

    component ToolTip: Rectangle {
        property alias text: caption.text
        x: parent.width + 7
        y: (parent.height - height) / 2
        width: caption.implicitWidth + 16
        height: 26
        radius: 3
        color: "#252525"
        border.color: "#666666"
        z: 10

        Text {
            id: caption
            anchors.centerIn: parent
            color: "#eeeeee"
            font.pixelSize: 12
        }
    }
}
