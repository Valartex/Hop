import QtQuick

import Style 1.0

Text {
    lineHeight: 38 + LanguageUiController.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: AmneziaStyle.color.paleGray
    font.pixelSize: 32
    font.weight: 700
    font.family: (Qt.application.name === "Hop" && Qt.platform.os === "windows") ? "Segoe UI" : "PT Root UI VF"
    font.letterSpacing: -1.0

    wrapMode: Text.WordWrap
}

