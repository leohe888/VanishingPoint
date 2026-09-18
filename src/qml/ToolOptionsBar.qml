import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import VanishingPoint 1.0

Rectangle {
    id: root

    property VpCanvas canvas

    color: "#535353"
    border.color: "#3e3e3e"
    border.width: 1

    // 只显示当前工具有意义的选项
    readonly property bool brushOptionsVisible: root.canvas !== null
                                                && root.canvas.tool === VpCanvas.Brush

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 18
        visible: root.brushOptionsVisible

        OptionSlider {
            label: qsTr("直径")
            from: 1
            to: 500
            suffix: " px"
            value: root.canvas ? root.canvas.brushDiameter : 42
            onMoved: (value) => root.canvas.brushDiameter = value
        }

        OptionSlider {
            label: qsTr("硬度")
            from: 0
            to: 100
            value: root.canvas ? root.canvas.brushHardness : 75
            onMoved: (value) => root.canvas.brushHardness = value
        }

        OptionSlider {
            label: qsTr("不透明度")
            from: 1
            to: 100
            value: root.canvas ? root.canvas.brushOpacity : 100
            onMoved: (value) => root.canvas.brushOpacity = value
        }

        RowLayout {
            spacing: 8

            Text {
                Layout.alignment: Qt.AlignVCenter
                color: "#dddddd"
                font.pixelSize: 12
                text: qsTr("颜色")
            }

            Rectangle {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignVCenter
                radius: 2
                color: root.canvas ? root.canvas.brushColor : "#e85d4a"
                border.color: "#777777"

                MouseArea {
                    anchors.fill: parent
                    onClicked: colorDialog.open()
                }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        title: qsTr("画笔颜色")
        selectedColor: root.canvas ? root.canvas.brushColor : "#e85d4a"
        onAccepted: root.canvas.brushColor = selectedColor
    }
}
