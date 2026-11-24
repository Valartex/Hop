import QtQuick

import Style 1.0

Text {
    lineHeight: 16 + LanguageUiController.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: AmneziaStyle.color.mutedGray
    font.pixelSize: 13
    font.weight: 400
    font.family: (Qt.application.name === "Hop" && Qt.platform.os === "windows") ? "Segoe UI" : "PT Root UI VF"
    font.letterSpacing: 0.02

    wrapMode: Text.WordWrap
}
