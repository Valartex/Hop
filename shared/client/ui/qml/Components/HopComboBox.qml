import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

ComboBox {
    id: control
    palette.windowText: HopStyle.text
    palette.text: HopStyle.text
    palette.buttonText: HopStyle.text
    palette.highlight: HopStyle.accent
    palette.highlightedText: HopStyle.background
    palette.disabled.text: HopStyle.muted
    palette.disabled.buttonText: HopStyle.muted
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
    popup.palette.windowText: HopStyle.text
    popup.palette.text: HopStyle.text
    popup.palette.buttonText: HopStyle.text
    popup.palette.highlight: HopStyle.accent
    popup.palette.highlightedText: HopStyle.background
    popup.background: Rectangle {
        color: HopStyle.section
        border.color: HopStyle.muted
        radius: HopStyle.radius
    }
}
