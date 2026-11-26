pragma Singleton
import QtQuick
QtObject {
    id: theme
    readonly property bool mobile: true
    readonly property bool dark: typeof AndroidAppearance !== "undefined" && AndroidAppearance.dark
    readonly property color background: dark ? "#0f1514" : "#f4fbf8"
    readonly property color surface: dark ? "#1b211f" : "#e8efec"
    readonly property color section: dark ? "#26352f" : "#dbe5df"
    readonly property color field: dark ? "#242b28" : "#e2eae6"
    readonly property color border: dark ? "#89938e" : "#6f7973"
    readonly property color text: dark ? "#dfe4df" : "#171d1a"
    readonly property color muted: dark ? "#bfc9c2" : "#3f4943"
    readonly property color accent: dark ? "#80d6b0" : "#006c4e"
    readonly property color accentText: dark ? "#003825" : "#ffffff"
    readonly property color accentContainer: dark ? "#005139" : "#9cf2cb"
    readonly property color accentContainerText: dark ? "#9cf2cb" : "#002114"
    readonly property color hover: dark ? "#303b35" : "#d2ded6"
    readonly property color danger: dark ? "#ffb4ab" : "#ba1a1a"
    readonly property color success: dark ? "#80d6b0" : "#006d3d"
    readonly property color healthError: danger
    readonly property int radius: 16
    readonly property int controlHeight: 48
    readonly property int buttonWidth: 160
    readonly property int settingsControlWidth: 230
    readonly property Palette palette: Palette {
        window: theme.background; windowText: theme.text
        base: theme.field; alternateBase: theme.surface; text: theme.text
        button: theme.surface; buttonText: theme.text
        highlight: theme.accent; highlightedText: theme.accentText
        placeholderText: theme.muted; accent: theme.accent
        disabled.text: theme.muted; disabled.windowText: theme.muted; disabled.buttonText: theme.muted
    }
}
