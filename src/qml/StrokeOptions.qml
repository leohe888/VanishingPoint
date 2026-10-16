import QtQuick
import QtQuick.Controls.Basic as Controls
import VanishingPoint 1.0
import QtQuick.Layouts

RowLayout {
    id: root
    required property int diameter
    required property int hardness
    required property int strokeOpacity
    required property int healMode
    signal healModeEdited(int value)
    signal diameterEdited(int value)
    signal hardnessEdited(int value)
    signal opacityEdited(int value)
    spacing: 18

    OptionInput {
        label: qsTr("直径"); from: 1; to: 500; suffix: " px"
        value: root.diameter
        onEdited: (value) => root.diameterEdited(value)
    }
    OptionInput {
        label: qsTr("硬度"); from: 0; to: 100
        value: root.hardness
        onEdited: (value) => root.hardnessEdited(value)
    }
    OptionInput {
        label: qsTr("不透明度"); from: 1; to: 100
        value: root.strokeOpacity
        onEdited: (value) => root.opacityEdited(value)
    }
    Text { text: qsTr("修复"); color: VpTheme.text }
    Controls.ComboBox {
            palette.text: VpTheme.text
            palette.button: VpTheme.inputBackground
            palette.buttonText: VpTheme.text
        model: [qsTr("关"), qsTr("明亮度"), qsTr("开")]
        currentIndex: root.healMode
        onActivated: root.healModeEdited(currentIndex)
    }
}
