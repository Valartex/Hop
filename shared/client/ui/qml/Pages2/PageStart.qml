import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root
    readonly property bool desktopHop: Qt.application.name === "Hop" && Qt.platform.os === "windows"
    readonly property bool hopClient: Qt.application.name === "Hop"
    readonly property bool mobileHop: hopClient && Qt.platform.os === "android"
    readonly property bool mobileHome: mobileHop
        && tabBarStackView.currentItem?.objectName === PageController.getPagePath(PageEnum.PageHome)
    readonly property var mobileTitles: [qsTr("Подключения"), qsTr("Маршруты"), qsTr("Автопереключение"), qsTr("Настройки")]

    property int desktopSection: 0
    function openDesktopPage(section, page) {
        desktopSection = section
        tabBarStackView.goToTabBarPage(page)
    }

    property bool isControlsDisabled: false
    property bool isTabBarDisabled: false
    property bool isTabBarShown: true

    Connections {
        objectName: "pageControllerConnection"

        target: PageController

        function onGoToPageHome() {
            if (PageController.isStartPageVisible() && !root.hopClient) {
                root.isTabBarShown = false
                tabBarStackView.goToTabBarPage(PageEnum.PageSetupWizardStart)
            } else {
                root.isTabBarShown = true
                root.desktopSection = 0
                tabBar.setCurrentIndex(0)
                tabBarStackView.goToTabBarPage(PageEnum.PageHome)
            }
        }

        function onGoToPageSettings() {
            if (root.hopClient) root.desktopSection = 3
            tabBar.setCurrentIndex(2)
            tabBarStackView.goToTabBarPage(PageEnum.PageSettings)
        }

        function onGoToPageViewConfig() {
            var pagePath = PageController.getPagePath(PageEnum.PageSetupWizardViewConfig)
            tabBarStackView.push(pagePath, { "objectName" : pagePath }, StackView.PushTransition)
        }

        function onGoToShareConnectionPage(headerText, configContentHeaderText, configCaption, configExtension, configFileName) {
            var pagePath = PageController.getPagePath(PageEnum.PageShareConnection)
            tabBarStackView.push(pagePath,
                                 { "objectName" : pagePath,
                                     "headerText" : headerText,
                                     "configContentHeaderText" : configContentHeaderText,
                                     "configCaption" : configCaption,
                                     "configExtension" : configExtension,
                                     "configFileName" : configFileName
                                 },
                                 StackView.PushTransition)
        }

        function onDisableControls(disabled) {
            isControlsDisabled = disabled
        }

        function onDisableTabBar(disabled) {
            isTabBarDisabled = disabled
        }

        function onClosePage() {
            if (tabBarStackView.depth <= 1) {
                PageController.hideWindow()
                return
            }
            tabBarStackView.pop()
            if (root.mobileHome) root.desktopSection = 0
        }

        function onGoToPage(page, slide) {
            if (root.hopClient && page === PageEnum.PageSettingsBackup) {
                root.openDesktopPage(3, PageEnum.PageSettings)
                Qt.callLater(function() { tabBarStackView.currentItem.showBackupSection() })
                return
            }
            if (root.hopClient && page === PageEnum.PageSettingsSplitTunneling) root.desktopSection = 1
            else if (root.hopClient && page === PageEnum.PageHopLocations) root.desktopSection = 2
            var pagePath = PageController.getPagePath(page)

            if (slide) {
                tabBarStackView.push(pagePath, { "objectName" : pagePath }, StackView.PushTransition)
            } else {
                tabBarStackView.push(pagePath, { "objectName" : pagePath }, StackView.Immediate)
            }
        }

        function onGoToStartPage() {
            while (tabBarStackView.depth > 1) {
                tabBarStackView.pop()
            }
        }

        function onEscapePressed() {
            if (root.isControlsDisabled || root.isTabBarDisabled) {
                return
            }

            var pageName = tabBarStackView.currentItem.objectName
            if ((pageName === PageController.getPagePath(PageEnum.PageShare)) ||
                    (pageName === PageController.getPagePath(PageEnum.PageSettings)) ||
                    (pageName === PageController.getPagePath(PageEnum.PageSetupWizardConfigSource))) {
                PageController.goToPageHome()
            } else {
                PageController.closePage()
            }
        }
    }

    Connections {
        objectName: "connectionControllerConnections"

        target: ConnectionController

        function onNoInstalledContainers() {
            PageController.setTriggeredByConnectButton(true)

            ServersUiController.setProcessedServerId(ServersUiController.defaultServerId)
            PageController.goToPage(PageEnum.PageSetupWizardEasy)
        }
    }

    Connections {
        objectName: "installControllerConnections"

        target: InstallController

        function onInstallationErrorOccurred(error) {
            PageController.showBusyIndicator(false)

            PageController.showErrorMessage(error)

            var needCloseCurrentPage = false
            var currentPageName = tabBarStackView.currentItem.objectName

            if (currentPageName === PageController.getPagePath(PageEnum.PageSetupWizardInstalling)) {
                needCloseCurrentPage = true
            } else if (currentPageName === PageController.getPagePath(PageEnum.PageDeinstalling)) {
                needCloseCurrentPage = true
            }
            if (needCloseCurrentPage) {
                PageController.closePage()
            }
        }

        function onWrongInstallationUser(message) {
            onInstallationErrorOccurred(message)
        }

        function onUpdateContainerFinished(message, closePage) {
            PageController.showNotificationMessage(message)
            if (closePage) {
                PageController.closePage()
            }
        }

        function onCachedProfileCleared(message) {
            PageController.showNotificationMessage(message)
        }

        function onRemoveServerFinished(finishedMessage) {
            if (!ServersUiController.getServersCount()) {
                PageController.goToPageHome()
            } else {
                PageController.goToStartPage()
                PageController.goToPage(PageEnum.PageSettingsServersList)
            }
            PageController.showNotificationMessage(finishedMessage)
        }

        function onRemoveAllContainersFinished(finishedMessage) {
            if (tabBarStackView.currentItem.objectName === PageController.getPagePath(PageEnum.PageDeinstalling)) {
                PageController.closePage()
            }
            PageController.showNotificationMessage(finishedMessage)
        }

        function onRemoveContainerFinished(finishedMessage) {
            if (tabBarStackView.currentItem.objectName === PageController.getPagePath(PageEnum.PageDeinstalling)) {
                PageController.closePage()
            }
            PageController.closePage()
            PageController.showNotificationMessage(finishedMessage)
        }
    }

    Connections {
        objectName: "importControllerConnections"

        target: ImportController

        function onImportErrorOccurred(error, goToPageHome) {
            PageController.showErrorMessage(error)
        }

        function onRestoreAppConfig(data) {
            PageController.showBusyIndicator(true)
            SettingsController.restoreAppConfigFromData(data)
            PageController.showBusyIndicator(false)
        }
    }

    Connections {
        objectName: "settingsControllerConnections"

        target: SettingsController

        function onLoggingDisableByWatcher() {
            PageController.showNotificationMessage(qsTr("Logging was disabled after 14 days, log files were deleted"))
        }

        function onRestoreBackupFinished() {
            PageController.showNotificationMessage(qsTr("Settings restored from backup file"))
            PageController.goToPageHome()
        }

        function onLoggingStateChanged() {
            if (SettingsController.isLoggingEnabled) {
                var message = qsTr("Logging is enabled. Note that logs will be automatically" +
                                   "disabled after 14 days, and all log files will be deleted.")
                PageController.showNotificationMessage(message)
            }
        }
    }

    Connections {
        target: SubscriptionUiController

        function onErrorOccurred(error) {
            PageController.showErrorMessage(error)
        }
    }

    Connections {
        target: SubscriptionUiController

        function onApiConfigRemoved(message) {
            PageController.showNotificationMessage(message)
        }

        function onApiServerRemoved(message) {
            if (!ServersUiController.getServersCount()) {
                PageController.goToPageHome()
            } else {
                PageController.goToStartPage()
                PageController.goToPage(PageEnum.PageSettingsServersList)
            }
            PageController.showNotificationMessage(message)
        }

        function onInstallServerFromApiFinished(message, preferredDefaultIndex) {
            PageController.goToPageHome()
            PageController.showNotificationMessage(message)
        }

        function onBackgroundPurchaseCompleted(message) {
            PageController.showNotificationMessage(message)
        }

        function onChangeApiCountryFinished(message) {
            PageController.goToPageHome()
            PageController.showNotificationMessage(message)
        }

        function onReloadServerFromApiFinished(message) {
            PageController.goToPageHome()
            PageController.showNotificationMessage(message)
        }
    }

    Rectangle {
        id: desktopNavigation
        objectName: "hopSidebar"
        visible: root.desktopHop
        width: visible ? 184 : 0
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        color: HopStyle.surface
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            RowLayout {
                Layout.bottomMargin: 16
                Image { source: "qrc:/images/hop/logo-preview.png"; sourceSize.width: 32; sourceSize.height: 32; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
                Label { text: "Hop"; color: HopStyle.text; font.pixelSize: 23; font.weight: Font.DemiBold }
            }
            HopButton {
                objectName: "hopHomeTab"
                text: qsTr("Подключения")
                Layout.fillWidth: true
                checkable: true
                checked: root.desktopSection === 0
                enabled: !root.isControlsDisabled && !root.isTabBarDisabled
                onClicked: PageController.goToPageHome()
            }
            HopButton {
                objectName: "hopRoutesButton"
                text: qsTr("Маршруты")
                Layout.fillWidth: true
                checkable: true
                checked: root.desktopSection === 1
                enabled: !root.isControlsDisabled && !root.isTabBarDisabled
                onClicked: root.openDesktopPage(1, PageEnum.PageSettingsSplitTunneling)
            }
            HopButton {
                objectName: "hopLocationPolicyButton"
                text: qsTr("Автопереключение")
                Layout.fillWidth: true
                checkable: true
                checked: root.desktopSection === 2
                enabled: !root.isControlsDisabled && !root.isTabBarDisabled
                onClicked: root.openDesktopPage(2, PageEnum.PageHopLocations)
            }
            HopButton {
                objectName: "hopSettingsTab"
                text: qsTr("Настройки")
                Layout.fillWidth: true
                checkable: true
                checked: root.desktopSection === 3
                enabled: !root.isControlsDisabled && !root.isTabBarDisabled
                onClicked: PageController.goToPageSettings()
            }
            Item { Layout.fillHeight: true }
            HopButton {
                text: qsTr("Назад")
                Layout.fillWidth: true
                visible: tabBarStackView.depth > 1
                enabled: !root.isControlsDisabled
                onClicked: PageController.closePage()
            }
            HopButton {
                objectName: "hopAboutButton"
                text: qsTr("О Hop")
                Layout.fillWidth: true
                checkable: true
                checked: root.desktopSection === 4
                enabled: !root.isControlsDisabled && !root.isTabBarDisabled
                onClicked: root.openDesktopPage(4, PageEnum.PageSettingsAbout)
            }
            Label { text: SettingsController.getAppVersion(); color: HopStyle.muted; font.pixelSize: 12; Layout.leftMargin: 10 }
        }
    }

    Loader {
        id: mobileNavigation
        objectName: "hopMobileNavigation"
        active: root.mobileHop && root.isTabBarShown
        visible: active
        x: 16
        y: 12
        z: 1
        source: active ? "qrc:/ui/qml/Components/HopMobileNavigation.qml" : ""
        onLoaded: {
            item.selectedIndex = Qt.binding(function() { return root.desktopSection })
            item.labels = Qt.binding(function() { return root.mobileTitles })
            item.enabled = Qt.binding(function() { return !root.isControlsDisabled && !root.isTabBarDisabled })
        }
        Connections {
            target: mobileNavigation.item
            function onActivated(index) {
                root.openDesktopPage(index, [PageEnum.PageHome, PageEnum.PageSettingsSplitTunneling, PageEnum.PageHopLocations, PageEnum.PageSettings][index])
            }
        }
    }

    Label {
        visible: mobileNavigation.visible && !root.mobileHome
        anchors.left: mobileNavigation.right
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: mobileNavigation.verticalCenter
        text: root.mobileTitles[root.desktopSection] || "Hop"
        color: HopStyle.text
        font.pixelSize: 22
        font.weight: Font.Medium
        Accessible.name: text
    }

    StackViewType {
        id: tabBarStackView
        objectName: "tabBarStackView"

        anchors.top: parent.top
        anchors.topMargin: mobileNavigation.visible && !root.mobileHome ? 60 : 0
        anchors.right: parent.right
        anchors.left: root.desktopHop ? desktopNavigation.right : parent.left
        anchors.bottom: root.mobileHop ? parent.bottom : tabBar.top
        anchors.bottomMargin: root.mobileHop ? Math.max(PageController.safeAreaBottomMargin, PageController.imeHeight) : 0

        enabled: !root.isControlsDisabled

        Binding { target: tabBarStackView; property: "palette"; value: HopStyle.palette; when: root.hopClient }
        background: Rectangle { visible: root.hopClient; color: HopStyle.background }

        function goToTabBarPage(page) {
            var pagePath = PageController.getPagePath(page)
            if (root.mobileHop && depth && get(0).objectName === PageController.getPagePath(PageEnum.PageHome)) {
                if (depth > 1) pop(get(0), StackView.Immediate)
                if (page !== PageEnum.PageHome) push(pagePath, { "objectName": pagePath }, StackView.Immediate)
                return
            }
            tabBarStackView.clear(StackView.Immediate)
            tabBarStackView.replace(pagePath, { "objectName" : pagePath }, StackView.Immediate)
        }

        Component.onCompleted: {
            var pagePath
            if (PageController.isStartPageVisible() && !root.hopClient) {
                root.isTabBarShown = false
                pagePath = PageController.getPagePath(PageEnum.PageSetupWizardStart)
            } else {
                root.isTabBarShown = true
                pagePath = PageController.getPagePath(PageEnum.PageHome)
                ServersUiController.setProcessedServerId(ServersUiController.defaultServerId)
            }

            tabBarStackView.push(pagePath, { "objectName" : pagePath })
        }

        Keys.onPressed: function(event) {
            switch (event.key) {
            case Qt.Key_Tab:
            case Qt.Key_Down:
            case Qt.Key_Right:
                FocusController.nextKeyTabItem()
                break
            case Qt.Key_Backtab:
            case Qt.Key_Up:
            case Qt.Key_Left:
                FocusController.previousKeyTabItem()
                break
            default:
                PageController.keyPressEvent(event.key)
                event.accepted = true
            }
        }
    }

    TabBar {
        id: tabBar
        objectName: "tabBar"

        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom

        visible: !root.hopClient && root.isTabBarShown
                 && !(tabBarStackView.currentItem && tabBarStackView.currentItem.isTabBarHidden === true)

        // Also adjust TabBar position when keyboard appears (Android 14+ workaround)
        anchors.bottomMargin: PageController.imeHeight

        topPadding: 8
        bottomPadding: 8 + PageController.safeAreaBottomMargin
        leftPadding: 96
        rightPadding: 96

        height: visible ? homeTabButton.implicitHeight + tabBar.topPadding + tabBar.bottomPadding : 0

        enabled: !root.isControlsDisabled && !root.isTabBarDisabled

        background: Shape {
            objectName: "backgroundShape"

            width: parent.width
            height: parent.height

            ShapePath {
                startX: 0
                startY: 0

                PathLine { x: width; y: 0 }
                PathLine { x: width; y: tabBar.height - 1 }
                PathLine { x: 0; y: tabBar.height - 1 }
                PathLine { x: 0; y: 0 }

                strokeWidth: 1
                strokeColor: AmneziaStyle.color.slateGray
                fillColor: AmneziaStyle.color.onyxBlack
            }
        }

        TabImageButtonType {
            id: homeTabButton
            objectName: "homeTabButton"

            isSelected: tabBar.currentIndex === 0
            image: "qrc:/images/controls/home.svg"
            clickedFunc: function () {
                tabBarStackView.goToTabBarPage(PageEnum.PageHome)
                ServersUiController.setProcessedServerId(ServersUiController.defaultServerId)
                tabBar.currentIndex = 0
            }
        }

        TabImageButtonType {
            id: shareTabButton
            objectName: "shareTabButton"

            Connections {
                target: ServersModel

                function onModelReset() {
                    if (!SettingsController.isOnTv()) {
                        var hasServerWithWriteAccess = ServersUiController.hasServerWithWriteAccess()
                        shareTabButton.visible = hasServerWithWriteAccess
                        shareTabButton.width = hasServerWithWriteAccess ? undefined : 0
                    }
                }
            }

            visible: !SettingsController.isOnTv() && ServersUiController.hasServerWithWriteAccess()
            width: !SettingsController.isOnTv() && ServersUiController.hasServerWithWriteAccess() ? undefined : 0

            isSelected: tabBar.currentIndex === 1
            image: "qrc:/images/controls/share-2.svg"
            clickedFunc: function () {
                tabBarStackView.goToTabBarPage(PageEnum.PageShare)
                tabBar.currentIndex = 1
            }
        }

        TabImageButtonType {
            id: settingsTabButton
            objectName: "settingsTabButton"

            isSelected: tabBar.currentIndex === 2
            image: (ServersUiController.hasServersFromGatewayApi && NewsModel.hasUnread && SettingsController.isNewsNotificationsEnabled()) ? "qrc:/images/controls/settings-news.svg" : "qrc:/images/controls/settings.svg"
            Binding {
                target: settingsTabButton
                property: "defaultColor"
                value: "transparent"
                when: (ServersUiController.hasServersFromGatewayApi && NewsModel.hasUnread)
            }
            clickedFunc: function () {
                tabBarStackView.goToTabBarPage(PageEnum.PageSettings)
                tabBar.currentIndex = 2
            }
        }

        TabImageButtonType {
            id: plusTabButton
            objectName: "plusTabButton"

            isSelected: tabBar.currentIndex === 3
            image: "qrc:/images/controls/plus.svg"
            clickedFunc: function () {
                tabBarStackView.goToTabBarPage(PageEnum.PageSetupWizardConfigSource)
                tabBar.currentIndex = 3
            }
        }
    }
}
