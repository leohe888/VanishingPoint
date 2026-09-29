import QtQuick 2.15

pragma ComponentBehavior: Bound

Rectangle {
    id: root

    property int toolId: -1
    property string tooltip: ""
    property bool tooltipBelow: false
    property string iconSource: ""
    property string shortcut: ""
    property bool selected: false
    property color hoverColor: "#454545"
    property color selectedColor: "#363636"

    width: 28
    height: 28
    radius: 2
    opacity: root.enabled ? 1.0 : 0.35
    color: root.selected
           ? root.selectedColor
           : (root.enabled && mouseArea.containsMouse ? root.hoverColor : "transparent")
    border.width: (root.selected || (root.enabled && mouseArea.containsMouse)) ? 1 : 0
    border.color: "#777777"

    signal activated(int toolId)

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
        onClicked: root.activated(root.toolId)

        ToolTip {
            visible: root.enabled && mouseArea.containsMouse && root.tooltip.length > 0
            text: root.tooltip
        }
    }

    Shortcut {
        enabled: root.enabled && root.shortcut.length > 0
        sequence: root.shortcut
        onActivated: root.activated(root.toolId)
    }

    component ToolTip: Rectangle {
        property alias text: caption.text
        x: root.tooltipBelow ? 0 : parent.width + 7
        y: root.tooltipBelow ? parent.height + 7 : (parent.height - height) / 2
        width: caption.implicitWidth + 16
        height: 26
        radius: 3
        color: "#F9F9F9"
        border.color: "#666666"
        z: 10

        Text {
            id: caption
            anchors.centerIn: parent
            color: "#575757"
            font.pixelSize: 12
        }
    }
}
