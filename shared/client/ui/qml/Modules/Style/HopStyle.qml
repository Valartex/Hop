pragma Singleton
import QtQuick
QtObject {
    id: theme
    readonly property bool mobile: false
    readonly property color background: "#101418"
    readonly property color surface: "#1a2028"
    readonly property color section: "#24313c"
    readonly property color field: "#131a21"
    readonly property color border: "#35424e"
    readonly property color text: "#eef2f5"
    readonly property color muted: "#9aabb9"
    readonly property color accent: "#63d6cf"
    readonly property color hover: "#26333f"
    readonly property color danger: "#f28b8b"
    readonly property color success: "#76cda0"
    readonly property color healthError: "#e88484"
    readonly property int radius: 4
    readonly property int controlHeight: Qt.platform.os === "android" ? 48 : 36
    readonly property int buttonWidth: 160
    readonly property Palette palette: Palette {
        window: theme.background
        windowText: theme.text
        base: theme.field
        alternateBase: theme.surface
        text: theme.text
        button: theme.surface
        buttonText: theme.text
        highlight: theme.accent
        highlightedText: theme.background
        placeholderText: theme.muted
        accent: theme.accent
        disabled.text: theme.muted
        disabled.windowText: theme.muted
        disabled.buttonText: theme.muted
    }
}
