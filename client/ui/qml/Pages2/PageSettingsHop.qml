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
    ScrollView {
        id: settingsScroll
        objectName: "hopSettingsScroll"
        anchors.fill: parent
        anchors.margins: 20
        contentWidth: availableWidth
        clip: true
        rightPadding: 16
        ScrollBar.vertical: HopScrollBar {
            objectName: "hopSettingsScrollBar"
            parent: settingsScroll
            x: settingsScroll.width - width
            y: settingsScroll.topPadding
            height: settingsScroll.availableHeight
        }
        ColumnLayout {
            width: settingsScroll.availableWidth
            spacing: 16
            Label { text: qsTr("Настройки"); font.pixelSize: 22; font.weight: Font.DemiBold; color: HopStyle.text }
            HopSettingsGroup {
                title: qsTr("Интерфейс")
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    Label { text: qsTr("Язык приложения"); Layout.fillWidth: true; color: HopStyle.text }
                    HopComboBox {
                        objectName: "hopApplicationLanguage"
                        Layout.preferredWidth: 230
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
                        checked: SettingsController.autoStartEnabled
                        onToggled: SettingsController.toggleAutoStart(checked)
                    }
                    HopCheckBox {
                        text: qsTr("Запускать свёрнутым в трей")
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
                title: qsTr("Автоматическое обновление")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Данные подписок"); Layout.fillWidth: true; color: HopStyle.text }
                        HopComboBox {
                            objectName: "hopSubscriptionInterval"
                            Layout.preferredWidth: 190
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
                    Label {
                        text: qsTr("Обновлять список серверов и сведения обо всех подключённых VPN-сервисах.")
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: HopStyle.muted
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Статус и задержка локаций"); Layout.fillWidth: true; color: HopStyle.text }
                        HopComboBox {
                            objectName: "hopHealthInterval"
                            Layout.preferredWidth: 190
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
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Собирать статистику"); Layout.fillWidth: true; color: HopStyle.text }
                        HopComboBox {
                            objectName: "hopHealthScope"
                            Layout.preferredWidth: 230
                            model: [qsTr("Все локации"), qsTr("Локации в пуле"), qsTr("Активная локация"), qsTr("Отключено")]
                            currentIndex: root.preferences ? root.preferences.healthScope : 0
                            onActivated: if (root.preferences) root.preferences.healthScope = currentIndex
                        }
                    }
                    Label {
                        text: qsTr("Область сбора определяет фоновые проверки и запись истории. При отключении история сохраняется, но новые замеры в неё не записываются. Контроль активного туннеля для автопереключения и его текущая задержка работают отдельно. AmneziaWG вне активного туннеля проверяется при отключённом VPN; новые конфигурации ради замеров не выдаются.")
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: HopStyle.muted
                    }
                }
            }
            HopSettingsGroup {
                title: qsTr("Данные приложения")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    Label { text: qsTr("Сохранение и восстановление настроек и подключений."); wrapMode: Text.WordWrap; Layout.fillWidth: true; color: HopStyle.muted }
                    HopButton { text: qsTr("Резервная копия"); onClicked: PageController.goToPage(PageEnum.PageSettingsBackup) }
                }
            }
            HopSettingsGroup {
                title: qsTr("Диагностика")
                Layout.fillWidth: true
                HopCheckBox {
                    text: qsTr("Сохранять диагностические журналы")
                    checked: SettingsController.isLoggingEnabled
                    onToggled: SettingsController.toggleLogging(checked)
                }
            }
        }
    }
}
