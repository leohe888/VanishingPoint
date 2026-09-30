import QtQuick 2.15
import QtQuick.Layouts 1.15
import VanishingPoint 1.0

Rectangle {
    color: VpTheme.panelBackground
    border.color: VpTheme.border
    border.width: 1

    property alias message: caption.text

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6

        Image {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            Layout.alignment: Qt.AlignVCenter
            source: "qrc:/assets/icons/hint.png"
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        Text {
            id: caption
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            verticalAlignment: Text.AlignVCenter
            color: VpTheme.text
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
