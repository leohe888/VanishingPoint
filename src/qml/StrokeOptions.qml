import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    required property int diameter
    required property int hardness
    required property int strokeOpacity
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
}
