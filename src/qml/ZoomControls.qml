import QtQuick 2.15
import QtQuick.Controls 2.15 as Controls
import QtQuick.Layouts 1.15
import VanishingPoint 1.0

RowLayout {
    id: root
    required property VpCanvas canvas

    readonly property var zoomItems: [
        {label: "6%", scale: 0.063}, {label: "12%", scale: 0.125},
        {label: "25%", scale: 0.25}, {label: "33%", scale: 0.333},
        {label: "50%", scale: 0.5}, {label: "66%", scale: 0.667},
        {label: "", scale: 0}, {label: "100%", scale: 1}, {label: "", scale: 0},
        {label: "200%", scale: 2}, {label: "300%", scale: 3},
        {label: "400%", scale: 4}, {label: "600%", scale: 6},
        {label: "800%", scale: 8}, {label: "1000%", scale: 10},
        {label: "1200%", scale: 12}, {label: "1600%", scale: 16},
        {label: "", scale: 0}, {label: qsTr("实际像素"), scale: 1},
        {label: qsTr("符合视图大小"), scale: -1},
        {label: qsTr("按屏幕大小缩放"), scale: -2}
    ]
    spacing: 3
    Image {
        Layout.preferredWidth: 13; Layout.preferredHeight: 13
        source: "qrc:/assets/icons/minus.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        MouseArea {
            anchors.fill: parent
            onClicked: root.canvas.zoomStep(true)
        }
    }
    Image {
        Layout.preferredWidth: 13; Layout.preferredHeight: 13
        source: "qrc:/assets/icons/plus.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        MouseArea {
            anchors.fill: parent
            onClicked: root.canvas.zoomStep(false)
        }
    }
    Rectangle {
        id: zoomSelector
        Layout.preferredWidth: 92; Layout.preferredHeight: 15
        color: "#414141"
        border.color: "#727272"
        radius: 2
        Text {
            anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
            color: "#eeeeee"
            text: Number((root.canvas.zoom * 100).toFixed(1)) + "%"
            font.pixelSize: 12
        }
        Text {
            anchors { right: parent.right; rightMargin: 7; verticalCenter: parent.verticalCenter }
            text: "▴"; color: "#dddddd"
        }
        MouseArea { anchors.fill: parent; onClicked: zoomPopup.open() }
        Controls.Popup {
            id: zoomPopup
            parent: zoomSelector
            x: 0
            y: -height - 2
            width: 164
            height: Math.min(menuColumn.implicitHeight + 8, root.canvas.height)
            padding: 4
            closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
            background: Rectangle { color: "#414141"; border.color: "#777777"; radius: 2 }
            contentItem: Flickable {
                contentHeight: menuColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: menuColumn
                    width: parent.width
                    Repeater {
                        model: root.zoomItems
                        delegate: Rectangle {
                            required property var modelData
                            width: menuColumn.width
                            height: modelData.scale === 0 ? 9 : 23
                            color: optionMouse.containsMouse ? "#626262" : "transparent"
                            Rectangle {
                                visible: modelData.scale === 0
                                anchors.centerIn: parent
                                width: parent.width - 10; height: 1; color: "#737373"
                            }
                            Text {
                                anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                                text: modelData.label; color: "#eeeeee"; font.pixelSize: 12
                            }
                            MouseArea {
                                id: optionMouse
                                anchors.fill: parent
                                enabled: modelData.scale !== 0
                                hoverEnabled: true
                                onClicked: {
                                    if (modelData.scale < 0) root.canvas.fitView(modelData.scale === -2)
                                    else root.canvas.setZoom(modelData.scale)
                                    zoomPopup.close()
                                    root.canvas.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
