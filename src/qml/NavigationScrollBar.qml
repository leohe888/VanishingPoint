import QtQuick 2.15
import QtQuick.Templates 2.15 as Templates

Rectangle {
    id: root
    property int orientation: Qt.Horizontal
    property real size: 1
    property real position: 0
    property real stepSize: 0.02
    readonly property bool horizontal: orientation === Qt.Horizontal
    readonly property real arrowExtent: Math.min(horizontal ? height : width,
                                                (horizontal ? width : height) / 2)
    signal moved(real position)

    color: "#4A4A4A"
    visible: size < 1

    function moveBy(direction) {
        moved(Math.max(0, Math.min(1 - size, position + direction * stepSize)))
    }

    Repeater {
        model: 2
        delegate: Rectangle {
            id: arrow
            required property int index
            readonly property int direction: index === 0 ? -1 : 1
            width: root.horizontal ? root.arrowExtent : root.width
            height: root.horizontal ? root.height : root.arrowExtent
            x: root.horizontal && index === 1 ? root.width - width : 0
            y: !root.horizontal && index === 1 ? root.height - height : 0
            color: button.pressed ? "#696969" : "#4A4A4A"

            Image {
                anchors.centerIn: parent
                width: Math.min(15, parent.width)
                height: Math.min(15, parent.height)
                source: arrow.index === 0 ? "qrc:/assets/icons/left-triangle.png"
                                          : "qrc:/assets/icons/right-triangle.png"
                rotation: root.horizontal ? 0 : 90
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            MouseArea {
                id: button
                anchors.fill: parent
                onClicked: root.moveBy(arrow.direction)
            }
        }
    }

    Templates.ScrollBar {
        id: track
        x: root.horizontal ? root.arrowExtent : 0
        y: root.horizontal ? 0 : root.arrowExtent
        width: root.horizontal ? Math.max(0, root.width - 2 * root.arrowExtent) : root.width
        height: root.horizontal ? root.height : Math.max(0, root.height - 2 * root.arrowExtent)
        orientation: root.orientation
        padding: 0
        minimumSize: 0.04
        size: root.size
        position: root.position
        active: true
        onPositionChanged: if (pressed) root.moved(position)
        background: Rectangle { color: "#4A4A4A" }
        contentItem: Rectangle { color: "#696969"; radius: 0 }
    }
}
