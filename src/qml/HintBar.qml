import QtQuick 2.15

Rectangle {
    color: "#535353"
    border.color: "#3e3e3e"
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
            color: "#dddddd"
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
