import QtQuick

import Style 1.0

Text {
    lineHeight: 24
    lineHeightMode: Text.FixedHeight

    color: AmneziaStyle.color.textPrimary
    font.pixelSize: 18
    font.weight: 400
    font.family: (Qt.application.name === "Hop" && Qt.platform.os === "windows") ? "Segoe UI" : "PT Root UI VF"
    font.letterSpacing: -0.4

    wrapMode: Text.WordWrap
}
