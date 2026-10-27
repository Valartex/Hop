import QtQuick
import Style 1.0
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

HopDialog {
    id: root
    objectName: "hopGeoDataDialog"
    title: qsTr("Файлы маршрутов")
    modal: true
    anchors.centerIn: parent
    width: Math.min(600, parent ? parent.width - 32 : 600)
    height: Math.min(660, parent ? parent.height - 32 : 660)
    standardButtons: Dialog.Close
    property var datasets: RouteDatasets
    property var routingPolicy: null
    property string selectedCategory: ""
    property bool addingRule: false
    Connections {
        target: root.routingPolicy
        function onChanged() {
            if (root.addingRule && !root.routingPolicy.busy) {
                root.addingRule = false
                if (!root.routingPolicy.lastError) root.close()
            }
        }
    }
    readonly property string selectedKind: kindBox.currentIndex === 0 ? "geoip" : "geosite"
    readonly property var currentSource: datasets.sources ? (datasets.sources[selectedKind] || ({})) : ({})
    function sourceUrl() { return currentSource.url || defaultLink() }
    function defaultLink() {
        return "https://github.com/kepler103b/geo-for-ru-only/releases/download/latest/" + selectedKind + ".dat"
    }
    onOpened: {
        selectedCategory = ""
        sourceLink.text = sourceUrl()
        datasets.loadCached(selectedKind)
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true
            text: root.routingPolicy ? qsTr("Загрузите набор, выберите категорию и добавьте правило. Обновления набора сохранят выбранное направление.")
                                     : qsTr("Просмотр и сохранение GeoIP/GeoSite. Для применения нужна категория в правилах маршрутов.")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        HopComboBox {
            id: kindBox
            objectName: "hopGeoDataKind"
            Layout.fillWidth: true
            model: [qsTr("GeoIP — IP-подсети"), qsTr("GeoSite — домены")]
            enabled: !root.datasets.busy
            onActivated: {
                root.selectedCategory = ""
                search.text = ""
                sourceLink.text = root.sourceUrl()
                root.datasets.loadCached(root.selectedKind)
            }
        }
        HopTextField {
            id: sourceLink
            objectName: "hopGeoDataUrl"
            Layout.fillWidth: true
            placeholderText: "https://…/" + root.selectedKind + ".dat"
            enabled: !root.datasets.busy
            selectByMouse: true
            maximumLength: 16384
        }
        RowLayout {
            Layout.fillWidth: true
            HopButton {
                Layout.fillWidth: true
                text: root.datasets.busy ? qsTr("Обработка…") : qsTr("Загрузить / обновить")
                enabled: !root.datasets.busy
                onClicked: root.datasets.download(sourceLink.text.trim(), root.selectedKind)
            }
            HopButton {
                Layout.fillWidth: true
                text: qsTr("Выбрать файл")
                enabled: !root.datasets.busy
                onClicked: fileDialog.open()
            }
        }
        HopCheckBox {
            objectName: "hopGeoDataAutoRefresh"
            Layout.fillWidth: true
            text: qsTr("Автоматически проверять обновления каждый день")
            checked: !!root.currentSource.url && root.currentSource.auto !== false
            enabled: !root.datasets.busy && !!root.currentSource.url
            onClicked: root.datasets.setAutoRefresh(root.selectedKind, checked)
        }
        Label {
            Layout.fillWidth: true
            visible: !!root.currentSource.updated
            text: qsTr("Последняя проверка: ") + (root.currentSource.updated ? new Date(root.currentSource.updated).toLocaleString(Qt.locale()) : "")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            text: root.datasets.lastError
            visible: text.length > 0
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: "#e7a276"
        }
        Label {
            Layout.fillWidth: true
            visible: root.datasets.byteCount > 0 && root.datasets.kind === root.selectedKind
            text: root.datasets.kind.toUpperCase() + qsTr(" · Категории: ") + root.datasets.categories.length
                  + " · " + Math.max(1, Math.round(root.datasets.byteCount / 1024)) + qsTr(" КиБ")
            textFormat: Text.PlainText
        }
        HopTextField {
            id: search
            Layout.fillWidth: true
            placeholderText: qsTr("Найти категорию: RU, CATEGORY-RU, VK…")
        }
        ListView {
            objectName: "hopGeoDataCategories"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.datasets.kind === root.selectedKind ? root.datasets.categories : []
            ScrollBar.vertical: HopScrollBar { id: categoriesScroll }
            delegate: RadioDelegate {
                id: categoryRow
                required property var modelData
                objectName: "hopGeoDataCategory_" + modelData.code
                width: ListView.view.width - (categoriesScroll.visible ? categoriesScroll.width : 0)
                implicitHeight: HopStyle.controlHeight
                visible: modelData.code.indexOf(search.text.toUpperCase()) >= 0
                height: visible ? implicitHeight : 0
                text: modelData.code + " · " + modelData.entries
                      + (modelData.reverseMatch ? qsTr(" · обратное совпадение") : "")
                checked: root.selectedCategory === modelData.code
                enabled: !modelData.reverseMatch
                onClicked: root.selectedCategory = modelData.code
                contentItem: Label {
                    text: categoryRow.text
                    color: categoryRow.enabled ? HopStyle.text : HopStyle.muted
                    rightPadding: categoryRow.indicator.width + categoryRow.spacing
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: categoryRow.checked ? HopStyle.hover : categoryRow.hovered ? HopStyle.surface : HopStyle.field
                    radius: HopStyle.radius
                    border.width: categoryRow.checked ? 1 : 0
                    border.color: HopStyle.accent
                }
            }
        }
        Label {
            objectName: "hopGeoDataSelectionHint"
            visible: root.routingPolicy !== null
            Layout.fillWidth: true
            text: root.datasets.busy ? qsTr("Дождитесь завершения загрузки набора.")
                  : root.datasets.kind !== root.selectedKind || root.datasets.categories.length === 0 ? qsTr("Сначала загрузите набор маршрутов.")
                  : root.selectedCategory.length === 0 ? qsTr("Выберите одну категорию в списке выше, чтобы добавить правило.")
                  : qsTr("Выбрана категория: %1").arg(root.selectedCategory)
            color: root.selectedCategory.length > 0 ? HopStyle.accent : HopStyle.muted
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        RowLayout {
            visible: root.routingPolicy !== null
            Layout.fillWidth: true
            HopComboBox {
                id: categoryTarget
                Layout.fillWidth: true
                model: [qsTr("Через VPN"), qsTr("Напрямую"), qsTr("Блокировать")]
            }
            HopButton {
                objectName: "hopGeoDataAddRule"
                text: qsTr("Добавить правило")
                emphasized: enabled
                enabled: root.routingPolicy && !root.routingPolicy.busy && !root.datasets.busy
                         && root.datasets.kind === root.selectedKind && root.selectedCategory.length > 0
                onClicked: root.addingRule = root.routingPolicy.addRule(root.selectedKind, root.selectedCategory, ["vpn", "direct", "block"][categoryTarget.currentIndex])
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.routingPolicy ? root.routingPolicy.lastError : ""
            visible: text.length > 0
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            color: "#e7a276"
        }
    }
    FileDialog {
        id: fileDialog
        title: qsTr("Выберите ") + root.selectedKind + ".dat"
        nameFilters: [qsTr("Файлы DAT (*.dat)"), qsTr("Все файлы (*)")]
        onAccepted: root.datasets.importFile(selectedFile, root.selectedKind)
    }
}
