import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import VanishingPoint 1.0

Rectangle {
    id: root

    property VpController controller

    color: "#535353"
    border.color: "#3e3e3e"
    border.width: 1

    // 只显示当前工具有意义的选项
    readonly property bool brushOptionsVisible: root.controller !== null
                                                && root.controller.tool === VpController.Brush
    readonly property bool cloneOptionsVisible: root.controller !== null
                                                && root.controller.tool === VpController.CloneStamp
    readonly property bool planeOptionsVisible: root.controller !== null
                                                && (root.controller.tool === VpController.CreatePlane
                                                    || root.controller.tool === VpController.EditPlane)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 18
        visible: root.brushOptionsVisible

        StrokeOptions {
            diameter: root.controller ? root.controller.brushDiameter : 42
            hardness: root.controller ? root.controller.brushHardness : 75
            strokeOpacity: root.controller ? root.controller.brushOpacity : 100
            onDiameterEdited: (value) => root.controller.brushDiameter = value
            onHardnessEdited: (value) => root.controller.brushHardness = value
            onOpacityEdited: (value) => root.controller.brushOpacity = value
        }

        RowLayout {
            Layout.fillWidth: false
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
                color: root.controller ? root.controller.brushColor : "#e85d4a"
                border.color: "#777777"

                MouseArea {
                    anchors.fill: parent
                    onClicked: colorDialog.open()
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    ColorDialog {
        id: colorDialog
        title: qsTr("画笔颜色")
        selectedColor: root.controller ? root.controller.brushColor : "#e85d4a"
        onAccepted: root.controller.brushColor = selectedColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 18
        visible: root.cloneOptionsVisible

        StrokeOptions {
            diameter: root.controller ? root.controller.cloneDiameter : 42
            hardness: root.controller ? root.controller.cloneHardness : 75
            strokeOpacity: root.controller ? root.controller.cloneOpacity : 100
            onDiameterEdited: (value) => root.controller.cloneDiameter = value
            onHardnessEdited: (value) => root.controller.cloneHardness = value
            onOpacityEdited: (value) => root.controller.cloneOpacity = value
        }

        RowLayout {
            Layout.fillWidth: false
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
                color: root.controller && root.controller.cloneAligned ? "#4bc3ff" : "#3e3e3e"
                border.color: "#777777"

                Text {
                    anchors.centerIn: parent
                    visible: root.controller ? root.controller.cloneAligned : false
                    text: "✓"
                    color: "#0e3d52"
                    font.pixelSize: 11
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.controller.cloneAligned = !root.controller.cloneAligned
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 18
        visible: root.planeOptionsVisible

        OptionInput {
            Layout.fillWidth: false
            label: qsTr("网格大小")
            from: 1
            to: 1000
            value: root.controller ? root.controller.gridSize : 50
            onEdited: (value) => root.controller.gridSize = value
        }

        OptionInput {
            Layout.fillWidth: false
            label: qsTr("角度")
            from: 0
            to: 360
            suffix: "°"
            enabled: root.controller ? root.controller.planeAngleEditable : false
            value: root.controller ? Math.round(root.controller.planeAngle) : 90
            onEdited: (value) => root.controller.planeAngle = value
        }

        Item {
            Layout.fillWidth: true
        }
    }
}
