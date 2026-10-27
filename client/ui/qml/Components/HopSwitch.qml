import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

Switch {
    id: control
    implicitHeight: HopStyle.controlHeight
    Layout.minimumHeight: HopStyle.controlHeight
    Layout.maximumHeight: HopStyle.controlHeight
    topPadding: 0
    bottomPadding: 0
    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? HopStyle.accent : HopStyle.field
        opacity: control.enabled ? 1 : 0.5
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? HopStyle.text : control.checked ? HopStyle.accent : HopStyle.border
        Rectangle {
            width: parent.height - 8
            height: width
            x: 4 + control.visualPosition * (parent.width - width - 8)
            y: 4
            radius: height / 2
            color: control.checked ? HopStyle.background : HopStyle.muted
            Behavior on x { NumberAnimation { duration: 80 } }
        }
    }
}
