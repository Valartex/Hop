import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

ColumnLayout {
    id: root
    property var rows: []
    property var columns: []
    property int currentIndex: -1
    property bool editingEnabled: true
    property bool removalEnabled: true
    property bool movementEnabled: editingEnabled
    property bool hasRefreshAction: false
    property real minimumColumnsWidth: 0
    property string emptyText: ""
    readonly property int count: rows ? rows.length : 0
    readonly property bool hasSelection: currentIndex >= 0 && currentIndex < count
    readonly property real rowHeight: HopStyle.mobile ? 48 : 40
    readonly property real columnAreaWidth: Math.max(minimumColumnsWidth, frame.width - 2 - (scroll.visible ? scroll.width : 0))
    signal moveRequested(int row, int delta)
    signal removeRequested(int row)
    signal refreshRequested(int row)
    signal cellToggled(int row, string role, bool checked)
    spacing: 8
    Layout.minimumWidth: 0
    Layout.minimumHeight: 164
    Layout.preferredHeight: HopStyle.mobile ? Math.max(164, Math.min(360, count * rowHeight + 84)) : -1
    activeFocusOnTab: true
    Accessible.role: Accessible.Table
    onCountChanged: if (currentIndex >= count)
        currentIndex = count - 1

    function selectRow(row) {
        currentIndex = row >= 0 && row < count ? row : -1;
        forceActiveFocus();
        if (hasSelection)
            list.positionViewAtIndex(currentIndex, ListView.Contain);
    }
    function cellText(row, column, index) {
        return column.role === "row" ? String(index + 1) : String(row[column.role] || "");
    }
    function columnWidth(index) {
        let fixed = 0, weights = 0;
        for (const column of columns) {
            if (column.stretch)
                weights += column.stretch;
            else
                fixed += column.width || 0;
        }
        const column = columns[index];
        return column.stretch ? Math.max(0, columnAreaWidth - fixed) * column.stretch / weights : column.width || 0;
    }
    Keys.onUpPressed: selectRow(Math.max(0, currentIndex - 1))
    Keys.onDownPressed: selectRow(Math.min(count - 1, currentIndex + 1))
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Home)
            selectRow(0);
        else if (event.key === Qt.Key_End)
            selectRow(count - 1);
        else if (event.key === Qt.Key_Delete && editingEnabled && removalEnabled && hasSelection)
            removeRequested(currentIndex);
        else {
            event.accepted = false;
            return;
        }
        event.accepted = true;
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        HopButton {
            objectName: root.objectName + "Up"
            text: "↑"
            iconOnly: true
            enabled: root.movementEnabled && root.hasSelection && root.currentIndex > 0
            Accessible.name: qsTr("Выше")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: root.moveRequested(root.currentIndex, -1)
        }
        HopButton {
            objectName: root.objectName + "Down"
            text: "↓"
            iconOnly: true
            enabled: root.movementEnabled && root.hasSelection && root.currentIndex + 1 < root.count
            Accessible.name: qsTr("Ниже")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: root.moveRequested(root.currentIndex, 1)
        }
        HopButton {
            objectName: root.objectName + "Remove"
            text: "×"
            iconOnly: true
            Accessible.name: qsTr("Удалить")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            enabled: root.editingEnabled && root.removalEnabled && root.hasSelection
            onClicked: root.removeRequested(root.currentIndex)
        }
        Item {
            Layout.fillWidth: true
        }
        HopButton {
            objectName: root.objectName + "Refresh"
            visible: root.hasRefreshAction
            text: qsTr("Обновить")
            enabled: root.hasRefreshAction && root.editingEnabled && root.removalEnabled && root.hasSelection && root.rows[root.currentIndex].sourceRule === true && !root.rows[root.currentIndex].updating
            onClicked: root.refreshRequested(root.currentIndex)
        }
    }
    Rectangle {
        id: frame
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: HopStyle.background
        border.color: HopStyle.border
        radius: HopStyle.radius
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 36
                color: HopStyle.section
                clip: true
                Row {
                    x: -list.contentX
                    Repeater {
                        model: root.columns
                        Label {
                            required property var modelData
                            required property int index
                            width: root.columnWidth(index)
                            height: 36
                            text: modelData.title
                            color: HopStyle.text
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                            rightPadding: 10
                            elide: Text.ElideRight
                            Rectangle {
                                visible: parent.index > 0
                                width: 1
                                height: parent.height
                                color: HopStyle.border
                            }
                        }
                    }
                }
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.rows
                contentWidth: root.columnAreaWidth
                flickableDirection: root.minimumColumnsWidth > 0 ? Flickable.AutoFlickIfNeeded : Flickable.VerticalFlick
                currentIndex: root.currentIndex
                keyNavigationEnabled: false
                ScrollBar.vertical: HopScrollBar {
                    id: scroll
                }
                ScrollBar.horizontal: HopScrollBar {
                    policy: root.minimumColumnsWidth > 0 && size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }
                delegate: Rectangle {
                    id: tableRow
                    required property var modelData
                    required property int index
                    objectName: root.objectName + "_row_" + index
                    width: root.columnAreaWidth
                    height: root.rowHeight
                    color: root.currentIndex === index ? HopStyle.section : rowHover.hovered ? HopStyle.hover : index % 2 ? HopStyle.background : HopStyle.surface
                    border.width: root.currentIndex === index ? 1 : 0
                    border.color: HopStyle.accent
                    Accessible.role: Accessible.Row
                    Accessible.selectable: true
                    Accessible.selected: root.currentIndex === index
                    Accessible.name: root.columns.map(column => root.cellText(modelData, column, index)).join(", ")
                    Accessible.onPressAction: root.selectRow(index)
                    HoverHandler {
                        id: rowHover
                    }
                    TapHandler {
                        onTapped: root.selectRow(tableRow.index)
                    }
                    Row {
                        Repeater {
                            model: root.columns
                            Item {
                                id: tableCell
                                required property var modelData
                                required property int index
                                width: root.columnWidth(index)
                                height: root.rowHeight
                                Label {
                                    objectName: root.objectName + "_" + parent.modelData.role + "_" + tableRow.index
                                    anchors.fill: parent
                                    visible: !parent.modelData.checkbox
                                    text: root.cellText(tableRow.modelData, parent.modelData, tableRow.index)
                                    textFormat: Text.PlainText
                                    color: HopStyle.text
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 10
                                    rightPadding: 10
                                    elide: parent.modelData.elideMiddle ? Text.ElideMiddle : Text.ElideRight
                                    ToolTip.visible: cellHover.hovered && truncated
                                    ToolTip.text: text
                                    HoverHandler {
                                        id: cellHover
                                    }
                                }
                                Loader {
                                    anchors.centerIn: parent
                                    active: tableCell.modelData.checkbox === true && tableRow.modelData.sourceRule === true
                                    width: item ? item.implicitWidth : 0
                                    height: item ? item.implicitHeight : 0
                                    sourceComponent: CheckBox {
                                        id: autoCheck
                                        objectName: root.objectName + "_auto_" + tableRow.index
                                        checked: Boolean(tableRow.modelData[tableCell.modelData.role])
                                        enabled: root.editingEnabled && root.removalEnabled
                                        Accessible.name: tableCell.modelData.title
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Автоматически обновлять каждый день")
                                        indicator: Rectangle {
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            x: autoCheck.leftPadding
                                            y: (autoCheck.height - height) / 2
                                            radius: 3
                                            color: autoCheck.checked ? HopStyle.accent : HopStyle.field
                                            border.color: autoCheck.checked ? HopStyle.accent : HopStyle.muted
                                            Label {
                                                anchors.centerIn: parent
                                                text: "✓"
                                                visible: autoCheck.checked
                                                color: HopStyle.background
                                                font.pixelSize: 13
                                                font.bold: true
                                            }
                                        }
                                        onClicked: {
                                            root.cellToggled(tableRow.index, tableCell.modelData.role, checked);
                                            checked = Qt.binding(function () {
                                                return Boolean(tableRow.modelData[tableCell.modelData.role]);
                                            });
                                        }
                                    }
                                }
                                Rectangle {
                                    visible: parent.index > 0
                                    width: 1
                                    height: parent.height
                                    color: HopStyle.border
                                    opacity: 0.5
                                }
                            }
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: HopStyle.border
                        opacity: 0.5
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    visible: root.count === 0
                    text: root.emptyText
                    color: HopStyle.muted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
