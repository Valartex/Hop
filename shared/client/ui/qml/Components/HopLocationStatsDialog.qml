import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

Item {
    id: root
    objectName: "hopLocationStatsDialog"
    visible: false
    width: 0
    height: 0
    property var controller: null
    property var info: ({})
    property string locationId: ""
    property bool checkAvailable: false
    property string notice: ""
    property int periodIndex: 4
    property string title: qsTr("Статистика локации") + (info.name ? " — " + info.name : "")
    property var statistics: ({
            samples: [],
            count: 0,
            failures: 0,
            minimum: -1,
            average: -1,
            maximum: -1
        })
    readonly property var points: statistics.samples || []
    signal checkRequested
    function refresh() {
        statistics = controller && locationId ? controller.history(locationId, [15, 60, 360, 1440, 10080][periodIndex]) : ({
                samples: []
            });
    }
    function open() {
        refresh();
        root.visible = true;
        if (HopStyle.mobile) {
            mobileDialog.open();
        } else {
            if (!statisticsWindow.positioned && statisticsWindow.transientParent) {
                const owner = statisticsWindow.transientParent;
                statisticsWindow.x = owner.x + (owner.width - statisticsWindow.width) / 2;
                statisticsWindow.y = owner.y + (owner.height - statisticsWindow.height) / 2;
                statisticsWindow.positioned = true;
            }
            statisticsWindow.raise();
            statisticsWindow.requestActivate();
        }
    }
    function close() {
        root.visible = false;
        if (mobileDialog.visible)
            mobileDialog.close();
    }
    function ms(number) {
        return number >= 0 ? Math.round(number) + qsTr(" мс") : "—";
    }
    function intervalLabel() {
        const seconds = Number(statistics.intervalMs || 0) / 1000;
        return seconds >= 60 ? qsTr("Интервал: %1 мин").arg(seconds / 60) : qsTr("Интервал: %1 с").arg(seconds);
    }
    Connections {
        target: root.controller
        function onHistoryChanged() {
            if (root.visible)
                root.refresh();
        }
    }
    Timer {
        interval: 30000
        repeat: true
        running: root.visible
        onTriggered: root.refresh()
    }
    Window {
        id: statisticsWindow
        objectName: "hopLocationStatsWindow"
        property bool positioned: false
        transientParent: root.Window.window
        modality: Qt.ApplicationModal
        flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint
        title: root.title
        width: 720
        height: 640
        minimumWidth: 420
        minimumHeight: 420
        color: HopStyle.surface
        visible: root.visible && !HopStyle.mobile
        onClosing: root.close()
        Loader {
            anchors.fill: parent
            anchors.margins: 16
            active: !HopStyle.mobile
            sourceComponent: statisticsBody
        }
        Shortcut {
            sequence: "Escape"
            enabled: statisticsWindow.visible
            onActivated: root.close()
        }
    }
    HopDialog {
        id: mobileDialog
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        title: root.title
        width: Math.min(720, parent ? parent.width - 32 : 720)
        height: Math.min(640, parent ? parent.height - 32 : 640)
        anchors.centerIn: parent
        onClosed: root.visible = false
        footer: null
        contentItem: Loader {
            active: HopStyle.mobile
            sourceComponent: statisticsBody
        }
    }
    Component {
        id: statisticsBody
        Pane {
            padding: 0
            font.family: Qt.platform.os === "windows" ? "Segoe UI" : "PT Root UI VF"
            font.pixelSize: 14
            palette.windowText: HopStyle.text
            palette.text: HopStyle.text
            palette.buttonText: HopStyle.text
            palette.base: HopStyle.field
            palette.button: HopStyle.surface
            palette.highlight: HopStyle.accent
            palette.highlightedText: HopStyle.background
            palette.placeholderText: HopStyle.muted
            background: Item {}
            contentItem: ColumnLayout {
                spacing: 12
                ScrollView {
                    id: statisticsScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.vertical: HopScrollBar {}
                    ColumnLayout {
                        width: statisticsScroll.availableWidth
                        height: Math.max(implicitHeight, statisticsScroll.availableHeight)
                        spacing: 10
                        Label {
                            Layout.fillWidth: true
                            text: root.info.api ? qsTr("Ответ DNS через подключённый VPN-туннель") : (root.info.protocol || "").indexOf("AmneziaWG") >= 0 ? qsTr("Ответ на проверку AmneziaWG") : qsTr("Ответ HTTPS через выбранную локацию")
                            color: HopStyle.muted
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            HopComboBox {
                                id: period
                                objectName: "hopLocationStatsPeriod"
                                Layout.fillWidth: true
                                model: [qsTr("15 минут"), qsTr("1 час"), qsTr("6 часов"), qsTr("24 часа"), qsTr("7 дней")]
                                currentIndex: root.periodIndex
                                onActivated: {
                                    root.periodIndex = currentIndex;
                                    root.refresh();
                                }
                            }
                            HopButton {
                                objectName: "hopLocationStatsCheck"
                                text: qsTr("Проверить")
                                enabled: root.checkAvailable
                                onClicked: root.checkRequested()
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            objectName: "hopLocationStatsNotice"
                            text: root.statistics.storageError || (root.statistics.loading ? qsTr("Загрузка истории…") : root.notice)
                            visible: text.length > 0
                            wrapMode: Text.WordWrap
                            color: HopStyle.muted
                        }
                        Label {
                            objectName: "hopLocationStatsSummary"
                            Layout.fillWidth: true
                            text: qsTr("Минимум: %1   Среднее: %2   Максимум: %3").arg(root.ms(root.statistics.minimum)).arg(root.ms(root.statistics.average)).arg(root.ms(root.statistics.maximum))
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Замеров: %1 · Без ответа: %2").arg(root.statistics.count || 0).arg(root.statistics.failures || 0)
                            color: HopStyle.muted
                        }
                        Label {
                            objectName: "hopLatencyScale"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: (graph.geometry.logarithmic ? qsTr("Время ответа, мс · логарифмическая шкала") : qsTr("Время ответа, мс")) + (root.statistics.aggregated ? " · " + root.intervalLabel() : "")
                            color: HopStyle.muted
                            font.pixelSize: 12
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 110
                            color: HopStyle.field
                            radius: HopStyle.radius
                            border.color: HopStyle.border
                            HopLatencyChart {
                                id: graph
                                anchors.fill: parent
                                anchors.margins: 8
                                samples: root.points
                                maximum: Math.max(0, root.statistics.maximum || 0)
                                windowStart: root.statistics.windowStart || 0
                                windowEnd: root.statistics.windowEnd || 0
                                aggregated: root.statistics.aggregated === true
                                visible: root.points.length > 0
                            }
                            Label {
                                anchors.centerIn: parent
                                width: parent.width - 32
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                visible: root.points.length === 0
                                text: qsTr("За выбранный период замеров нет.\nВыполните проверку или включите обновление статуса в настройках.")
                                color: HopStyle.muted
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: root.points.length ? Qt.formatDateTime(new Date(Number(root.statistics.windowStart || root.points[0].at)), "dd.MM HH:mm:ss") + " — " + Qt.formatDateTime(new Date(Number(root.statistics.windowEnd || root.points[root.points.length - 1].at)), "dd.MM HH:mm:ss") : ""
                            color: HopStyle.muted
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.fillWidth: true
                            text: (root.statistics.aggregated ? qsTr("Линия — среднее за интервал, полоса — минимум и максимум.") : qsTr("Линия — отдельные замеры.")) + " " + qsTr("Красная полоса снизу — замеры без ответа; яркость отражает их долю.") + "\n" + qsTr("История хранится 7 дней. Масштаб и детализация зависят от периода; сводные показатели учитывают все замеры.")
                            wrapMode: Text.WordWrap
                            color: HopStyle.muted
                            font.pixelSize: 12
                        }
                    }
                }
                HopButton {
                    objectName: "hopDialogCloseButton"
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Закрыть")
                    onClicked: root.close()
                }
            }
        }
    }
}
