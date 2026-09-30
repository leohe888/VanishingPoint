import QtQuick 2.15
import QtQuick.Layouts 1.15
import VanishingPoint 1.0

Rectangle {
    id: root
    color: VpTheme.canvasBackground
    border.color: VpTheme.border
    border.width: 1

    property alias canvas: canvas

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 1

            VpCanvas {
                id: canvas
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: vpController
                clip: true
            }

            Item {
                Layout.preferredWidth: 13
                Layout.fillHeight: true

                NavigationScrollBar {
                    orientation: Qt.Vertical
                    anchors.fill: parent
                    size: canvas.verticalSize
                    position: canvas.verticalPosition
                    onMoved: (value) => canvas.scrollTo(canvas.horizontalPosition, value)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 13
            color: VpTheme.panelBackground

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 3
                anchors.rightMargin: 15
                spacing: 8

                ZoomControls {
                    canvas: root.canvas
                    Layout.alignment: Qt.AlignVCenter
                }

                NavigationScrollBar {
                    orientation: Qt.Horizontal
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    size: canvas.horizontalSize
                    position: canvas.horizontalPosition
                    onMoved: (value) => canvas.scrollTo(value, canvas.verticalPosition)
                }
            }
        }
    }
}

