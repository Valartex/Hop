import QtQuick
import QtQuick.Controls
import Style 1.0

ScrollBar {
    id: control
    policy: size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    implicitWidth: 12
    implicitHeight: 12
    minimumSize: 0.06
    padding: 2
    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: control.pressed ? HopStyle.accent : control.hovered ? HopStyle.text : HopStyle.muted
    }
    background: Rectangle { color: HopStyle.field; radius: 4 }
}
