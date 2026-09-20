import QtQuick 2.15
import QtQuick.Layouts 1.15

// 工具选项行：标签 + 滑杆 + 数值。拖动滑杆或滚轮调整。
RowLayout {
    id: root

    property string label: ""
    property int from: 0
    property int to: 100
    property int value: from
    property string suffix: ""

    signal moved(int value)

    spacing: 8

    // 不可用时整行压暗；Item.enabled 本身已经会挡掉滑杆上的鼠标事件
    opacity: root.enabled ? 1.0 : 0.45

    // 把候选取值收进 [from, to]，变化时才通知外部
    function commit(candidate) {
        const clamped = Math.max(root.from, Math.min(root.to, Math.round(candidate)))
        if (clamped !== root.value)
            root.moved(clamped)
    }

    Text {
        Layout.preferredWidth: 44
        Layout.alignment: Qt.AlignVCenter
        color: "#dddddd"
        font.pixelSize: 12
        text: root.label
    }

    Rectangle {
        id: track
        Layout.preferredWidth: 110
        Layout.preferredHeight: 4
        Layout.alignment: Qt.AlignVCenter
        radius: 2
        color: "#3e3e3e"

        // 已选区间
        Rectangle {
            width: knob.x + knob.width / 2
            height: parent.height
            radius: parent.radius
            color: "#4bc3ff"
        }

        Rectangle {
            id: knob
            width: 10
            height: 10
            radius: 5
            anchors.verticalCenter: parent.verticalCenter
            x: (track.width - width) * (root.value - root.from) / Math.max(1, root.to - root.from)
            color: "#f3f8fa"
            border.color: "#0e526e"
        }

        MouseArea {
            // 命中区域比滑杆本身高，便于点中
            x: 0
            y: -9
            width: track.width
            height: track.height + 18

            onPressed: (mouse) => root.commit(root.from + mouse.x / width * (root.to - root.from))
            onPositionChanged: (mouse) => {
                if (mouse.pressed)
                    root.commit(root.from + mouse.x / width * (root.to - root.from))
            }
            onWheel: (wheel) => root.commit(root.value + (wheel.angleDelta.y > 0 ? 1 : -1))
        }
    }

    Text {
        Layout.preferredWidth: 44
        Layout.alignment: Qt.AlignVCenter
        color: "#f3f8fa"
        font.pixelSize: 12
        text: root.value + root.suffix
    }
}
