import QtQuick 2.15

pragma ComponentBehavior: Bound

Rectangle {
    id: root

    property string toolName: ""
    property string label: ""
    property string iconSource: ""
    property string shortcut: ""
    property bool selected: false
    property color hoverColor: "#454545"
    property color selectedColor: "#363636"

    width: 28
    height: 28
    radius: 2
    color: root.selected
           ? root.selectedColor
           : (mouseArea.containsMouse ? root.hoverColor : "transparent")
    border.width: (root.selected || mouseArea.containsMouse) ? 1 : 0
    border.color: "#777777"

    signal activated(string tool)

    Image {
        anchors.centerIn: parent
        width: 20
        height: 20
        source: "qrc:/assets/icons/" + root.iconSource
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: root.selected ? 1.0 : 0.82
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated(root.toolName)

        ToolTip {
            visible: mouseArea.containsMouse
            text: root.label
        }
    }

    Shortcut {
        sequence: root.shortcut
        onActivated: root.activated(root.toolName)
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
