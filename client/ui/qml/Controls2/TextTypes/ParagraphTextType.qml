import QtQuick
import Style 1.0

Text {
    lineHeight: 24 + LanguageUiController.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: AmneziaStyle.color.paleGray
    font.pixelSize: 16
    font.weight: 400
    font.family: (Qt.application.name === "Hop" && Qt.platform.os === "windows") ? "Segoe UI" : "PT Root UI VF"

    wrapMode: Text.WordWrap
}
