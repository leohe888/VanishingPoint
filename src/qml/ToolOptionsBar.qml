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
    readonly property bool cloneOptionsVisible: root.canvas !== null
                                                && root.canvas.tool === VpCanvas.CloneStamp
    readonly property bool planeOptionsVisible: root.canvas !== null
                                                && (root.canvas.tool === VpCanvas.CreatePlane
                                                    || root.canvas.tool === VpCanvas.EditPlane)

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

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 18
        visible: root.cloneOptionsVisible

        OptionSlider {
            label: qsTr("直径")
            from: 1
            to: 500
            suffix: " px"
            value: root.canvas ? root.canvas.cloneDiameter : 42
            onMoved: (value) => root.canvas.cloneDiameter = value
        }

        OptionSlider {
            label: qsTr("硬度")
            from: 0
            to: 100
            value: root.canvas ? root.canvas.cloneHardness : 75
            onMoved: (value) => root.canvas.cloneHardness = value
        }

        OptionSlider {
            label: qsTr("不透明度")
            from: 1
            to: 100
            value: root.canvas ? root.canvas.cloneOpacity : 100
            onMoved: (value) => root.canvas.cloneOpacity = value
        }

        RowLayout {
            spacing: 8

            Text {
                Layout.alignment: Qt.AlignVCenter
                color: "#dddddd"
                font.pixelSize: 12
                text: qsTr("对齐")
            }

            Rectangle {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                radius: 2
                color: root.canvas && root.canvas.cloneAligned ? "#4bc3ff" : "#3e3e3e"
                border.color: "#777777"

                Text {
                    anchors.centerIn: parent
                    visible: root.canvas ? root.canvas.cloneAligned : false
                    text: "✓"
                    color: "#0e3d52"
                    font.pixelSize: 11
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.canvas.cloneAligned = !root.canvas.cloneAligned
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 18
        visible: root.planeOptionsVisible

        OptionSlider {
            label: qsTr("网格大小")
            from: 1
            to: 1000
            value: root.canvas ? root.canvas.gridSize : 50
            onMoved: (value) => root.canvas.gridSize = value
        }

        OptionSlider {
            label: qsTr("角度")
            from: 0
            to: 360
            suffix: "°"
            enabled: root.canvas ? root.canvas.planeAngleEditable : false
            value: root.canvas ? Math.round(root.canvas.planeAngle) : 90
            onMoved: (value) => root.canvas.planeAngle = value
        }

        // 夹角不可调时把原因直接写在旁边，比 tooltip 更容易发现
        Text {
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            elide: Text.ElideRight
            color: "#9a9a9a"
            font.pixelSize: 11
            visible: root.canvas ? !root.canvas.planeAngleEditable : false
            text: root.canvas ? root.canvas.planeAngleLockReason : ""
        }
    }
}
