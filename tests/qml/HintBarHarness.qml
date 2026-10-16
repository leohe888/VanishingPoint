import QtQuick 2.15
import "../../src/qml"

Item {
    width: 900
    height: 40

    HintBar {
        objectName: "hintBar"
        anchors.fill: parent
        controller: vpController
    }
}
