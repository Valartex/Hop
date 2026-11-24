import QtQuick
import QtQuick.Controls
import Style 1.0
Button {
    id: control
    property bool emphasized: false
    property color emphasisColor: HopStyle.accent
    property bool iconOnly: false
    property string iconName: ""
    property bool isFocusable: true
    property string displayText: text
    implicitHeight: HopStyle.controlHeight
    implicitWidth: iconOnly ? HopStyle.controlHeight : HopStyle.buttonWidth
    font.family: Qt.platform.os === "windows" ? "Segoe UI" : "PT Root UI VF"
    font.pixelSize: 13
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    Accessible.name: displayText
    contentItem: Label {
        id: label
        text: control.displayText
        font: control.font
        color: control.emphasized ? HopStyle.background : HopStyle.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: HopStyle.radius
        color: control.emphasized ? control.emphasisColor : control.down || control.checked || control.hovered ? HopStyle.hover : HopStyle.surface
        border.width: 1
        border.color: control.emphasized ? control.emphasisColor : control.activeFocus || control.checked ? HopStyle.accent : HopStyle.border
    }
}
