import QtQuick 2.15
import QtQuick.Layouts 1.15
import VanishingPoint 1.0

Rectangle {
    id: hintBar
    color: VpTheme.panelBackground
    border.color: VpTheme.border
    border.width: 1

    property var controller: null
    readonly property string toolHint: controller ? controller.toolHint : ""
    readonly property string interactionHint: controller ? controller.interactionHint : ""
    property string temporaryMessage: ""
    property bool temporaryError: false

    function clearTemporaryMessage() {
        messageTimer.stop();
        temporaryMessage = "";
        temporaryError = false;
    }

    function showTemporaryMessage(text, isError) {
        temporaryMessage = text;
        temporaryError = isError;
        if (text.length > 0)
            messageTimer.restart();
        else
            messageTimer.stop();
    }

    onControllerChanged: clearTemporaryMessage()

    Timer {
        id: messageTimer
        interval: 3000
        onTriggered: hintBar.clearTemporaryMessage()
    }

    Connections {
        target: hintBar.controller
        function onStatusMessage(text, error) { hintBar.showTemporaryMessage(text, error); }
        function onStatusMessageCleared() { hintBar.clearTemporaryMessage(); }
    }

    RowLayout {
        id: contentRow
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
            objectName: "vanishingPointHintText"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            verticalAlignment: Text.AlignVCenter
            text: hintBar.temporaryMessage.length > 0 ? hintBar.temporaryMessage
                  : hintBar.interactionHint.length > 0 ? hintBar.interactionHint : hintBar.toolHint
            color: hintBar.temporaryMessage.length > 0 && hintBar.temporaryError
                   ? "#E60012" : VpTheme.text
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
