import QtQuick 2.15
import QtQuick.Layouts 1.15

// 工具选项行：标签 + 整数输入框；回车或失去焦点时提交。
RowLayout {
    id: root

    property string label: ""
    property int from: 0
    property int to: 100
    property int value: from
    property string suffix: ""

    signal edited(int value)

    spacing: 8
    opacity: root.enabled ? 1.0 : 0.45

    function restoreValue() {
        input.text = String(root.value)
    }

    function commit() {
        if (root.enabled && input.acceptableInput) {
            const candidate = Number(input.text)
            if (candidate !== root.value)
                root.edited(candidate)
        }
        // 非法输入或控制器拒绝修改时，显示实际值。
        root.restoreValue()
    }

    onValueChanged: root.restoreValue()
    onEnabledChanged: root.restoreValue()
    Component.onCompleted: root.restoreValue()

    Text {
        Layout.alignment: Qt.AlignVCenter
        color: "#dddddd"
        font.pixelSize: 12
        text: root.label
    }

    Rectangle {
        Layout.preferredWidth: 70
        Layout.preferredHeight: 24
        Layout.alignment: Qt.AlignVCenter
        radius: 2
        color: "#3e3e3e"
        border.color: input.activeFocus ? "#4bc3ff" : "#777777"

        TextInput {
            id: input
            anchors.fill: parent
            anchors.margins: 4
            color: "#f3f8fa"
            selectionColor: "#4bc3ff"
            selectedTextColor: "#0e3d52"
            font.pixelSize: 12
            horizontalAlignment: TextInput.AlignRight
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            activeFocusOnTab: true
            clip: true
            validator: IntValidator { bottom: root.from; top: root.to; locale: "C" }
            onAccepted: root.commit()
            onActiveFocusChanged: {
                if (!activeFocus)
                    root.commit()
            }
            Keys.onEscapePressed: (event) => {
                root.restoreValue()
                input.focus = false
                event.accepted = true
            }
        }
    }

    Text {
        Layout.alignment: Qt.AlignVCenter
        visible: root.suffix.length > 0
        color: "#dddddd"
        font.pixelSize: 12
        text: root.suffix
    }
}
