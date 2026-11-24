import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

TextField {
    id: control
    color: HopStyle.text
    placeholderTextColor: HopStyle.muted
    selectionColor: HopStyle.accent
    selectedTextColor: HopStyle.background
    selectByMouse: true
    implicitHeight: HopStyle.controlHeight
    Layout.minimumHeight: HopStyle.controlHeight
    Layout.maximumHeight: HopStyle.controlHeight
    background: Rectangle {
        color: HopStyle.section
        radius: HopStyle.radius
        border.color: control.activeFocus ? HopStyle.accent : HopStyle.border
    }
}
