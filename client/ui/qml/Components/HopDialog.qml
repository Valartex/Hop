import QtQuick
import QtQuick.Controls
import Style 1.0

Dialog {
    id: control
    padding: 16
    font.family: Qt.platform.os === "windows" ? "Segoe UI" : "PT Root UI"
    font.pixelSize: 14
    palette.window: HopStyle.surface
    palette.windowText: HopStyle.text
    palette.base: HopStyle.field
    palette.text: HopStyle.text
    palette.button: HopStyle.surface
    palette.buttonText: HopStyle.text
    palette.highlight: HopStyle.accent
    palette.highlightedText: HopStyle.background
    palette.placeholderText: HopStyle.muted
    header: Label {
        text: control.title
        textFormat: Text.PlainText
        visible: text.length > 0
        padding: 16
        color: HopStyle.text
        font.family: "Segoe UI"
        font.pixelSize: 16
        font.weight: Font.DemiBold
    }
    footer: DialogButtonBox {
        id: buttons
        function translateCloseButton() {
            const closeButton = standardButton(Dialog.Close)
            if (closeButton) {
                closeButton.objectName = "hopDialogCloseButton"
                // Qt обновляет text стандартной кнопки языком Windows после LanguageChange.
                closeButton.displayText = Qt.binding(function() { return qsTr("Закрыть") })
            }
        }
        Component.onCompleted: Qt.callLater(translateCloseButton)
        onStandardButtonsChanged: Qt.callLater(translateCloseButton)
        standardButtons: control.standardButtons
        delegate: HopButton {}
        padding: 12
        background: Rectangle { color: HopStyle.surface }
    }
    background: Rectangle {
        color: HopStyle.surface
        border.color: HopStyle.border
        radius: HopStyle.radius
    }
}
