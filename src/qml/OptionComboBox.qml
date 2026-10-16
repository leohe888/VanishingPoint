import QtQuick 2.15
import QtQuick.Controls.Basic as Controls
import VanishingPoint 1.0

Controls.ComboBox {
    id: root
    implicitWidth: 90
    implicitHeight: 24
    leftPadding: 8
    rightPadding: 26
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: 12
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.45

    onVisibleChanged: if (!visible) popup.close()
    onEnabledChanged: if (!enabled) popup.close()

    background: Rectangle {
        radius: 2
        color: root.down ? VpTheme.toolSelected
                         : root.hovered ? VpTheme.toolHover : VpTheme.inputBackground
        border.color: root.activeFocus ? VpTheme.accent : VpTheme.controlBorder
    }
    contentItem: Text {
        text: root.displayText
        font: root.font
        color: VpTheme.inputText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Image {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 10
        height: 10
        source: "qrc:/assets/icons/chevron-down.png"
        fillMode: Image.PreserveAspectFit
        rotation: root.popup.visible ? 180 : 0
        opacity: 0.85
    }
    delegate: Controls.ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: root.popup.availableWidth
        height: 26
        leftPadding: 24
        rightPadding: 8
        hoverEnabled: true
        text: String(modelData)
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: option.text
            font: root.font
            color: VpTheme.inputText
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 2
            color: option.highlighted ? VpTheme.popupHover : "transparent"
        }
        Image {
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 11
            height: 11
            source: "qrc:/assets/icons/check.png"
            fillMode: Image.PreserveAspectFit
            visible: root.currentIndex === option.index
        }
    }
    popup: Controls.Popup {
        y: root.height + 3
        width: root.width
        height: Math.min(options.contentHeight + topPadding + bottomPadding, 180)
        padding: 3
        margins: 6
        background: Rectangle {
            color: VpTheme.inputBackground
            border.color: VpTheme.controlBorder
            radius: 3
        }
        contentItem: ListView {
            id: options
            clip: true
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }
    }
}
