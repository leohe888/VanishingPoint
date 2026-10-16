import QtQuick 2.15
import QtQuick.Controls.Basic as Controls
import VanishingPoint 1.0
import QtQuick.Layouts 1.15

// Keep controller bindings intact while previewing slider, label and wheel edits.
RowLayout {
    id: root
    property string label: ""
    property int from: 0
    property int to: 100
    property int value: from
    property string suffix: ""
    property int stepSize: 1
    signal edited(int value)
    spacing: 8
    opacity: root.enabled ? 1.0 : 0.45

    function restoreValue() {
        input.text = String(root.value)
        valueSlider.value = root.value
    }
    function preview(candidate) {
        if (!root.enabled || !isFinite(candidate))
            return
        const bounded = Math.max(root.from, Math.min(root.to, Math.round(candidate)))
        if (bounded !== root.value)
            root.edited(bounded)
        // A constrained plane angle can be rejected by the controller.
        root.restoreValue()
    }
    function commit() {
        const candidate = input.text.trim().length > 0 ? Number(input.text) : NaN
        root.preview(candidate)
        root.restoreValue()
    }
    function adjustValueByWheel(deltaY) {
        if (!root.enabled || deltaY === 0)
            return
        let current = Number(input.text)
        if (input.text.trim().length === 0 || !isFinite(current))
            current = root.value
        input.focus = false
        root.preview(current + (deltaY > 0 ? root.stepSize : -root.stepSize))
    }
    onValueChanged: root.restoreValue()
    onEnabledChanged: {
        if (!enabled)
            sliderPopup.close()
        root.restoreValue()
    }
    onVisibleChanged: if (!visible) sliderPopup.close()
    Component.onCompleted: root.restoreValue()

    Text {
        Layout.alignment: Qt.AlignVCenter
        color: VpTheme.text
        font.pixelSize: 12
        text: root.label
        MouseArea {
            id: labelDrag
            objectName: "labelDrag"
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            property real pressedX: 0
            property int pressedValue: 0
            onPressed: (mouse) => {
                input.focus = false
                sliderPopup.close()
                pressedX = mouse.x
                pressedValue = root.value
            }
            onPositionChanged: (mouse) => {
                if (pressed)
                    root.preview(pressedValue + Math.round(mouse.x - pressedX) * root.stepSize)
            }
            onReleased: root.commit()
            onCanceled: root.commit()
        }
    }
    Rectangle {
        id: inputBox
        Layout.preferredWidth: 90
        Layout.preferredHeight: 24
        Layout.alignment: Qt.AlignVCenter
        radius: 2
        color: VpTheme.inputBackground
        border.color: input.activeFocus ? VpTheme.accent : VpTheme.controlBorder
        TextInput {
            id: input
            objectName: "valueInput"
            anchors.left: parent.left
            anchors.right: suffixText.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 4
            color: VpTheme.inputText
            selectionColor: VpTheme.accent
            selectedTextColor: VpTheme.selectedText
            font.pixelSize: 12
            horizontalAlignment: TextInput.AlignRight
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            activeFocusOnTab: true
            clip: true
            validator: IntValidator { bottom: -2147483647; top: 2147483647; locale: "C" }
            onActiveFocusChanged: {
                if (activeFocus)
                    selectAll()
                else
                    root.commit()
            }
            Keys.onReturnPressed: { root.commit(); input.focus = false }
            Keys.onEnterPressed: { root.commit(); input.focus = false }
            Keys.onEscapePressed: (event) => {
                root.restoreValue()
                input.focus = false
                event.accepted = true
            }
        }
        Text {
            id: suffixText
            anchors.right: arrowButton.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.suffix
            color: VpTheme.text
            font.pixelSize: 12
        }
        Controls.Button {
            id: arrowButton
            objectName: "sliderButton"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 20
            padding: 4
            background: Rectangle { color: "transparent" }
            contentItem: Image {
                source: "qrc:/assets/icons/chevron-down.png"
                fillMode: Image.PreserveAspectFit
            }
            onPressedChanged: {
                if (!pressed)
                    return
                input.focus = false
                if (sliderPopup.visible)
                    sliderPopup.close()
                else
                    sliderPopup.open()
            }
        }
        WheelHandler {
            target: null
            onWheel: (event) => {
                root.adjustValueByWheel(event.angleDelta.y)
                event.accepted = true
            }
        }
    }
    Controls.Popup {
        id: sliderPopup
        objectName: "sliderPopup"
        x: inputBox.x
        y: inputBox.y + inputBox.height
        width: 140
        height: 32
        padding: 6
        focus: true
        closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutsideParent
        background: Rectangle {
            color: VpTheme.inputBackground
            border.color: VpTheme.controlBorder
            radius: 4
        }
        contentItem: Controls.Slider {
            id: valueSlider
            objectName: "valueSlider"
            from: root.from
            to: root.to
            stepSize: root.stepSize
            snapMode: Controls.Slider.SnapAlways
            live: true
            wheelEnabled: false
            leftPadding: 6
            rightPadding: 6
            background: Rectangle {
                x: valueSlider.leftPadding
                y: (valueSlider.height - height) / 2
                width: valueSlider.availableWidth
                height: 4
                radius: 2
                color: VpTheme.controlBorder
                Rectangle {
                    width: valueSlider.visualPosition * parent.width
                    height: parent.height
                    radius: parent.radius
                    color: VpTheme.accent
                }
            }
            handle: Rectangle {
                x: valueSlider.leftPadding + valueSlider.visualPosition * valueSlider.availableWidth - width / 2
                y: (valueSlider.height - height) / 2
                width: 12
                height: 12
                radius: 6
                color: VpTheme.accent
                border.color: VpTheme.text
            }
            onMoved: root.preview(value)
            onPressedChanged: if (!pressed) root.commit()
        }
    }
}
