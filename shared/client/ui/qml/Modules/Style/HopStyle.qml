pragma Singleton
import QtQuick
QtObject {
    id: theme
    readonly property bool mobile: false
    readonly property bool dark: Qt.styleHints.colorScheme !== Qt.Light
    readonly property color background: dark ? "#101418" : "#f4f7f8"
    readonly property color surface: dark ? "#1a2028" : "#ffffff"
    readonly property color section: dark ? "#24313c" : "#e5edef"
    readonly property color field: dark ? "#131a21" : "#ffffff"
    readonly property color border: dark ? "#35424e" : "#b7c6cc"
    readonly property color text: dark ? "#eef2f5" : "#18262f"
    readonly property color muted: dark ? "#9aabb9" : "#506775"
    readonly property color accent: dark ? "#63d6cf" : "#087f7a"
    readonly property color hover: dark ? "#26333f" : "#dbeceb"
    readonly property color danger: dark ? "#f28b8b" : "#b73748"
    readonly property color success: dark ? "#76cda0" : "#23794f"
    readonly property color healthError: dark ? "#e88484" : "#b43f4c"
    readonly property int radius: 4
    readonly property int controlHeight: Qt.platform.os === "android" ? 48 : 36
    readonly property int buttonWidth: 160
    readonly property int settingsControlWidth: 230
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
