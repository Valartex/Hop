import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

ComboBox {
    id: control
    hoverEnabled: true
    implicitHeight: HopStyle.controlHeight
    Layout.minimumHeight: HopStyle.controlHeight
    Layout.maximumHeight: HopStyle.controlHeight
    background: Rectangle {
        implicitWidth: 120
        implicitHeight: HopStyle.controlHeight
        radius: HopStyle.radius
        color: control.enabled && (control.hovered || control.down) ? HopStyle.hover : HopStyle.section
        border.color: control.activeFocus ? HopStyle.accent : HopStyle.border
        opacity: control.enabled ? 1 : 0.55
    }
    popup.palette.window: HopStyle.section
    popup.background: Rectangle {
        color: HopStyle.section
        border.color: HopStyle.muted
        radius: HopStyle.radius
    }
}
