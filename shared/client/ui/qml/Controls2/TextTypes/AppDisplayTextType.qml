import QtQuick

import Style 1.0

Text {
    color: AmneziaStyle.color.textStaticWhite
    font.pixelSize: 36
    font.weight: 500
    font.family: (Qt.application.name === "Hop" && Qt.platform.os === "windows") ? "Segoe UI" : "PT Root UI VF"
    font.letterSpacing: -1.8
}
