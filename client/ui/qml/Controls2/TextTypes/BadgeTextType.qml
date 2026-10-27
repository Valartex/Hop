import QtQuick

import Style 1.0

Text {
    lineHeight: 10 + LanguageUiController.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: AmneziaStyle.color.midnightBlack
    font.pixelSize: 11
    font.weight: Font.Medium
    font.family: (Qt.application.name === "Hop" && Qt.platform.os === "windows") ? "Segoe UI" : "PT Root UI VF"

    wrapMode: Text.NoWrap
}
