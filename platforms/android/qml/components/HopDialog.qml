import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Style 1.0
Dialog {
    Material.theme: HopStyle.dark ? Material.Dark : Material.Light
    id: control
    padding: 24
    font.family: "Roboto"
    font.pixelSize: 14
    Material.roundedScale: Material.ExtraLargeScale
    Material.accent: HopStyle.accent
    background: Rectangle { radius: 28; color: HopStyle.surface }
    Overlay.modal: Rectangle { color: "#99000000" }
    header: Item {
        width: control.width
        implicitWidth: 0
        implicitHeight: titleLabel.contentHeight + 40
        visible: control.title.length > 0
        Text {
            id: titleLabel
            objectName: "hopDialogTitle"
            x: 24; y: 24
            width: control.width - 48
            height: contentHeight
            text: control.title
            color: HopStyle.text
            font.family: "Roboto"
            font.pixelSize: 24
            font.weight: Font.Normal
            wrapMode: Text.WordWrap
        }
    }
    footer: DialogButtonBox {
        standardButtons: control.standardButtons
        leftPadding: 24; rightPadding: 24; topPadding: 0; bottomPadding: 16
        background: Item {}
        delegate: HopButton { flat: true; implicitWidth: Math.max(48, implicitContentWidth + 24) }
    }
}
