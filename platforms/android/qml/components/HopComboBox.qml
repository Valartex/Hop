import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Style 1.0
ComboBox {
    id: control
    Material.theme: HopStyle.dark ? Material.Dark : Material.Light
    implicitHeight: Math.max(HopStyle.controlHeight, caption.contentHeight + topPadding + bottomPadding)
    Layout.minimumHeight: HopStyle.controlHeight
    Layout.minimumWidth: 0
    font.family: "Roboto"
    font.pixelSize: 16
    Material.accent: HopStyle.accent
    contentItem: Label {
        id: caption
        text: control.displayText
        textFormat: Text.PlainText
        font: control.font
        color: control.enabled ? HopStyle.text : HopStyle.muted
        leftPadding: control.mirrored ? 0 : 16
        rightPadding: control.mirrored ? 16 : 0
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
    delegate: ItemDelegate {
        required property int index
        width: control.popup.width
        implicitHeight: Math.max(HopStyle.controlHeight, option.contentHeight + topPadding + bottomPadding)
        highlighted: control.highlightedIndex === index
        contentItem: Label {
            id: option
            text: control.textAt(index)
            textFormat: Text.PlainText
            font: control.font
            color: HopStyle.text
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
        Accessible.name: option.text
    }
}
