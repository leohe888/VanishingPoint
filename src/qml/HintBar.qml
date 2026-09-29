import QtQuick 2.15
import VanishingPoint 1.0

Rectangle {
    color: VpTheme.panelBackground
    border.color: VpTheme.border
    border.width: 1

    property alias message: caption.text

    Row {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6

        Image {
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/assets/icons/hint.png"
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        Text {
            id: caption
            width: parent.width - 24
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            color: VpTheme.text
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
