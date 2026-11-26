import QtQuick
import Style 1.0
import QtQuick.Controls
import QtQuick.Layouts
import PageEnum 1.0
import "../Controls2"
import "../Components"

PageType {
    id: root
    enableTimer: false
    readonly property var preferences: typeof RefreshPreferences !== "undefined" ? RefreshPreferences : null
    readonly property var locations: typeof Locations !== "undefined" ? Locations : null
    readonly property int settingsComboWidth: HopStyle.settingsControlWidth
    function showBackupSection() {
        const flickable = settingsScroll.contentItem
        flickable.contentY = Math.max(0, flickable.contentHeight - flickable.height)
    }
    ScrollView {
        id: settingsScroll
        objectName: "hopSettingsScroll"
        anchors.fill: parent
        anchors.margins: HopStyle.mobile ? 16 : 20
        anchors.topMargin: HopStyle.mobile ? 4 : 20
        contentWidth: availableWidth
        clip: true
        rightPadding: HopStyle.mobile ? 0 : 16
        ScrollBar.vertical: HopScrollBar {
            objectName: "hopSettingsScrollBar"
            parent: settingsScroll
            x: settingsScroll.width - width
            y: settingsScroll.topPadding
            height: settingsScroll.availableHeight
        }
        ColumnLayout {
            width: settingsScroll.availableWidth
            spacing: HopStyle.mobile ? 10 : 16
            Label { visible: !HopStyle.mobile; text: qsTr("Настройки"); font.pixelSize: 22; font.weight: Font.DemiBold; color: HopStyle.text }
            HopSettingsGroup {
                title: qsTr("Интерфейс")
                Layout.fillWidth: true
                GridLayout {
                    columns: 1
                    anchors.fill: parent
                    Label { text: qsTr("Язык приложения"); Layout.fillWidth: true; color: HopStyle.text }
                    HopComboBox {
                        objectName: "hopApplicationLanguage"
                        Layout.preferredWidth: root.width < 600 ? -1 : root.settingsComboWidth
                        Layout.fillWidth: root.width < 600
                        Layout.minimumWidth: 0
                        model: ["English", "Русский", "简体中文"]
                        currentIndex: LanguageUiController.currentLanguageIndex
                        onActivated: LanguageUiController.changeLanguage(currentIndex)
                    }
                }
            }
            HopSettingsGroup {
                title: qsTr("Запуск")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    HopCheckBox {
                        text: qsTr("Запускать вместе с Windows")
                        visible: Qt.platform.os === "windows"
                        checked: SettingsController.autoStartEnabled
                        onToggled: SettingsController.toggleAutoStart(checked)
                    }
                    HopCheckBox {
                        text: qsTr("Запускать свёрнутым в трей")
                        visible: Qt.platform.os === "windows"
                        enabled: SettingsController.autoStartEnabled
                        checked: SettingsController.startMinimized
                        onToggled: SettingsController.toggleStartMinimized(checked)
                    }
                    HopCheckBox {
                        text: qsTr("Подключаться при запуске")
                        checked: SettingsController.isAutoConnectEnabled()
                        onToggled: SettingsController.toggleAutoConnect(checked)
                    }
                }
            }
            HopSettingsGroup {
                title: qsTr("Ограниченные сети")
                visible: Qt.platform.os === "android"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    HopComboBox {
                        objectName: "hopRestrictedNetworkMode"
                        Layout.fillWidth: true
                        model: [qsTr("Обычное подключение"), qsTr("Только профили обхода белых списков"), qsTr("Автоматически при недоступности")]
                        currentIndex: root.locations ? root.locations.restrictedMode : 0
                        enabled: root.locations !== null && !ConnectionController.isConnected && !ConnectionController.isConnectionInProgress && !ConnectionController.isDisconnecting
                        onActivated: if (root.locations) root.locations.restrictedMode = currentIndex
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: HopStyle.muted
                        text: qsTr("Отметьте профили обхода в меню локации. Автоматический режим попробует их после обычных локаций из пула.")
                    }
                    HopButton {
                        Layout.fillWidth: true
                        text: qsTr("Системные настройки VPN")
                        onClicked: SettingsController.openSystemVpnSettings()
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: HopStyle.muted
                        text: qsTr("Системная блокировка отключает интернет при остановке VPN, включая прямые маршруты.")
                    }
                }
            }
            HopSettingsGroup {
                title: qsTr("Автоматическое обновление")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    GridLayout {
                    columns: 1
                        Layout.fillWidth: true
                        Label { text: qsTr("Данные подписок"); Layout.fillWidth: true; color: HopStyle.text }
                        HopComboBox {
                            objectName: "hopSubscriptionInterval"
                            Layout.preferredWidth: root.width < 600 ? -1 : root.settingsComboWidth
                            Layout.fillWidth: root.width < 600
                            Layout.minimumWidth: 0
                            textRole: "label"; valueRole: "minutes"
                            model: [
                                {label: qsTr("Вручную"), minutes: 0}, {label: qsTr("Каждые 15 минут"), minutes: 15},
                                {label: qsTr("Каждые 30 минут"), minutes: 30}, {label: qsTr("Каждый час"), minutes: 60},
                                {label: qsTr("Каждые 3 часа"), minutes: 180}, {label: qsTr("Каждые 6 часов"), minutes: 360},
                                {label: qsTr("Каждые 12 часов"), minutes: 720}, {label: qsTr("Каждые сутки"), minutes: 1440}
                            ]
                            currentIndex: [0, 15, 30, 60, 180, 360, 720, 1440].indexOf(root.preferences ? root.preferences.subscriptionMinutes : 360)
                            onActivated: if (root.preferences) root.preferences.subscriptionMinutes = currentValue
                        }
                    }
                    GridLayout {
                    columns: 1
                        Layout.fillWidth: true
                        Label { text: qsTr("Статус и задержка локаций"); Layout.fillWidth: true; color: HopStyle.text }
                        HopComboBox {
                            objectName: "hopHealthInterval"
                            Layout.preferredWidth: root.width < 600 ? -1 : root.settingsComboWidth
                            Layout.fillWidth: root.width < 600
                            Layout.minimumWidth: 0
                            textRole: "label"; valueRole: "minutes"
                            model: [
                                {label: qsTr("Вручную"), minutes: 0}, {label: qsTr("Каждую минуту"), minutes: 1},
                                {label: qsTr("Каждые 5 минут"), minutes: 5}, {label: qsTr("Каждые 10 минут"), minutes: 10},
                                {label: qsTr("Каждые 30 минут"), minutes: 30}, {label: qsTr("Каждый час"), minutes: 60}
                            ]
                            currentIndex: [0, 1, 5, 10, 30, 60].indexOf(root.preferences ? root.preferences.healthMinutes : 0)
                            onActivated: if (root.preferences) root.preferences.healthMinutes = currentValue
                        }
                    }
                    GridLayout {
                    columns: 1
                        Layout.fillWidth: true
                        Label { text: qsTr("Собирать статистику"); Layout.fillWidth: true; color: HopStyle.text }
                        HopComboBox {
                            objectName: "hopHealthScope"
                            Layout.preferredWidth: root.width < 600 ? -1 : root.settingsComboWidth
                            Layout.fillWidth: root.width < 600
                            Layout.minimumWidth: 0
                            model: [qsTr("Все локации"), qsTr("Локации в пуле"), qsTr("Активная локация"), qsTr("Отключено")]
                            currentIndex: root.preferences ? root.preferences.healthScope : 0
                            onActivated: if (root.preferences) root.preferences.healthScope = currentIndex
                        }
                    }
                    Label {
                        text: qsTr("AmneziaWG вне текущего туннеля проверяется при отключённом VPN. Для замеров используются сохранённые конфигурации.")
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: HopStyle.muted
                    }
                }
            }
            HopSettingsGroup {
                title: qsTr("О Hop")
                visible: Qt.platform.os === "android"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    HopButton { Layout.fillWidth: true; visible: Qt.platform.os === "android"; text: qsTr("О Hop"); onClicked: PageController.goToPage(PageEnum.PageSettingsAbout) }
                }
            }
            HopSettingsGroup {
                objectName: "hopDiagnosticsSettingsGroup"
                title: qsTr("Диагностика")
                Layout.fillWidth: true
                HopCheckBox {
                    Layout.fillWidth: true
                    text: qsTr("Сохранять журналы")
                    checked: SettingsController.isLoggingEnabled
                    onToggled: SettingsController.toggleLogging(checked)
                }
            }
            HopSettingsGroup {
                objectName: "hopBackupSettingsGroup"
                title: qsTr("Резервная копия")
                Layout.fillWidth: true
                HopBackupControls { anchors.fill: parent }
            }
        }
    }
}
