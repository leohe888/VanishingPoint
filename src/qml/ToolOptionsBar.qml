import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import VanishingPoint 1.0

Rectangle {
    id: root

    color: VpTheme.panelBackground
    border.color: VpTheme.border
    border.width: 1

    // 只显示当前工具有意义的选项
    readonly property bool brushOptionsVisible: vpController.tool === VpTools.Brush
    readonly property bool cloneOptionsVisible: vpController.tool === VpTools.CloneStamp
    readonly property bool planeOptionsVisible: vpController.tool === VpTools.CreatePlane
                                               || vpController.tool === VpTools.EditPlane

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 18
        visible: root.brushOptionsVisible

        StrokeOptions {
            diameter: vpController.brushDiameter
            hardness: vpController.brushHardness
            strokeOpacity: vpController.brushOpacity
            onDiameterEdited: (value) => vpController.brushDiameter = value
            onHardnessEdited: (value) => vpController.brushHardness = value
            onOpacityEdited: (value) => vpController.brushOpacity = value
        }

        RowLayout {
            Layout.fillWidth: false
            spacing: 8

            Text {
                Layout.alignment: Qt.AlignVCenter
                color: VpTheme.text
                font.pixelSize: 12
                text: qsTr("颜色")
            }

            Rectangle {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignVCenter
                radius: 2
                color: vpController.brushColor
                border.color: VpTheme.controlBorder

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
        selectedColor: vpController.brushColor
        onAccepted: vpController.brushColor = selectedColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 18
        visible: root.cloneOptionsVisible

        StrokeOptions {
            diameter: vpController.cloneDiameter
            hardness: vpController.cloneHardness
            strokeOpacity: vpController.cloneOpacity
            onDiameterEdited: (value) => vpController.cloneDiameter = value
            onHardnessEdited: (value) => vpController.cloneHardness = value
            onOpacityEdited: (value) => vpController.cloneOpacity = value
        }

        RowLayout {
            Layout.fillWidth: false
            spacing: 8

            Text {
                Layout.alignment: Qt.AlignVCenter
                color: VpTheme.text
                font.pixelSize: 12
                text: qsTr("对齐")
            }

            Rectangle {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                radius: 2
                color: vpController.cloneAligned ? VpTheme.accent : VpTheme.checkboxBackground
                border.color: VpTheme.controlBorder

                Image {
                    anchors.centerIn: parent
                    visible: vpController.cloneAligned
                    width: 11
                    height: 11
                    source: "qrc:/assets/icons/check-dark.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: vpController.cloneAligned = !vpController.cloneAligned
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
            value: vpController.gridSize
            onEdited: (value) => vpController.gridSize = value
        }

        OptionInput {
            Layout.fillWidth: false
            label: qsTr("角度")
            from: 0
            to: 360
            suffix: "°"
            enabled: vpController.planeAngleEditable
            value: Math.round(vpController.planeAngle)
            onEdited: (value) => vpController.planeAngle = value
        }

        Item {
            Layout.fillWidth: true
        }
    }
}
