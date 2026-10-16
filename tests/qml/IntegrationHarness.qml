import QtQuick 2.15
import VanishingPoint 1.0
import "../../src/qml"
Item {
    width: 500; height: 400
    VpCanvas {
        id: canvas; objectName: "canvas"
        width: 500; height: 400
        controller: vpController
    }
    NavigationScrollBar {
        objectName: "navigation"; width: 500; height: 13
        size: canvas.horizontalSize; position: canvas.horizontalPosition
        onMoved: canvas.scrollTo(position, canvas.verticalPosition)
    }
    Shortcut {
        objectName: "redo"; sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]
        onActivated: canvas.redo()
    }
}
