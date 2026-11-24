import QtQuick
import Style 1.0
HopTextField {
    id: control
    background: Rectangle {
        color: HopStyle.field
        radius: HopStyle.radius
        border.color: control.activeFocus ? HopStyle.accent : HopStyle.border
    }
}
