import QtQuick 2.15
import VanishingPoint 1.0

Rectangle {
    id: root
    color: "#4D4D4D"
    border.color: "#3E3E3E"
    border.width: 1

    property alias canvas: canvas

    VpCanvas {
        id: canvas
        controller: vpController
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: bar.top; margins: 1; rightMargin: 15 }
        clip: true
    }

    NavigationScrollBar {
        id: vertical
        orientation: Qt.Vertical
        anchors { right: parent.right; top: canvas.top; bottom: canvas.bottom; rightMargin: 1 }
        width: 13
        size: canvas.verticalSize
        position: canvas.verticalPosition
        onMoved: (value) => canvas.scrollTo(canvas.horizontalPosition, value)
    }

    Rectangle {
        id: bar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
        height: 13
        color: "#535353"

        ZoomControls {
            id: zoomControls
            canvas: root.canvas
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 3 }
        }

        NavigationScrollBar {
            orientation: Qt.Horizontal
            anchors { left: zoomControls.right; leftMargin: 8; right: parent.right; rightMargin: 15; top: parent.top; bottom: parent.bottom }
            size: canvas.horizontalSize
            position: canvas.horizontalPosition
            onMoved: (value) => canvas.scrollTo(value, canvas.verticalPosition)
        }
    }
}

