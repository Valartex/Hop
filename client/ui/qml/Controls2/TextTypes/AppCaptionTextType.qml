import QtQuick

import Style 1.0

Text {
    lineHeight: 14
    lineHeightMode: Text.FixedHeight

    color: AmneziaStyle.color.textPrimary
    font.pixelSize: 12
    font.weight: 400
    font.family: (Qt.application.name === "Hop" && Qt.platform.os === "windows") ? "Segoe UI" : "PT Root UI VF"
    font.letterSpacing: 0

    wrapMode: Text.WordWrap
}
