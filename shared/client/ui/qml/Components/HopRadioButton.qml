import QtQuick
import QtQuick.Controls
import Style 1.0
RadioButton {
    id: control
    property bool isFocusable: true
    property color accentColor: HopStyle.accent
    property color secondaryColor: HopStyle.muted
    implicitWidth: 30
    implicitHeight: 40
    indicator: Rectangle {
        implicitWidth: 18; implicitHeight: 18
        x: 5; y: (control.height - height) / 2; radius: 9
        color: "transparent"
        border.width: control.activeFocus ? 2 : 1
        border.color: control.checked ? control.accentColor : control.secondaryColor
        Rectangle {
            anchors.centerIn: parent
            width: 8; height: 8; radius: 4
            color: control.accentColor; visible: control.checked
        }
    }
}
