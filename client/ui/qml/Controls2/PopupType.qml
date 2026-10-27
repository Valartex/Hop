import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Style 1.0

import "TextTypes"
import "../Config"

Popup {
    id: root

    readonly property bool desktopHop: Qt.application.name === "Hop" && Qt.platform.os === "windows"
    property string text
    property bool closeButtonVisible: true

    leftMargin: 25
    rightMargin: 25
    bottomMargin: 70 + PageController.safeAreaBottomMargin

    width: parent.width - leftMargin - rightMargin

    anchors.centerIn: parent
    modal: root.closeButtonVisible
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle {
        visible: root.closeButtonVisible
        color: AmneziaStyle.color.translucentMidnightBlack
    }

    onOpened: {
        timer.start()
    }

    onClosed: {
        FocusController.dropRootObject(root)
    }

    background: Rectangle {
        anchors.fill: parent

        color: root.desktopHop ? HopStyle.surface : "white"
        radius: 4
    }

    Timer {
        id: timer
        interval: 200 // Milliseconds
        onTriggered: {
            FocusController.pushRootObject(root)
            FocusController.setFocusItem(closeButton)
        }
        repeat: false // Stop the timer after one trigger
        running: true // Start the timer
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight

        anchors.fill: parent

        RowLayout {
            id: content

            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16

            CaptionTextType {
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true

                onLinkActivated: function(link) {
                    Qt.openUrlExternally(LanguageUiController.getCurrentDocsUrl(link))
                }

                color: root.desktopHop ? HopStyle.text : AmneziaStyle.color.midnightBlack
                textFormat: root.desktopHop ? Text.PlainText : Text.AutoText
                text: root.desktopHop ? root.text.replace(/<[^>]*>/g, "") : root.text

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }

            BasicButtonType {
                id: closeButton
                visible: closeButtonVisible

                implicitHeight: 32

                defaultColor: root.desktopHop ? HopStyle.surface : "white"
                hoveredColor: AmneziaStyle.color.lightGray
                pressedColor: AmneziaStyle.color.lightGray
                disabledColor: AmneziaStyle.color.charcoalGray

                textColor: root.desktopHop ? HopStyle.text : AmneziaStyle.color.midnightBlack
                borderWidth: 0

                text: qsTr("Close")

                clickedFunc: function() {
                    root.close()
                }
            }
        }
    }
}
