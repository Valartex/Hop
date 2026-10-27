import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
import "../Controls2"
import "../Components"
PageType {
    objectName: "hopLocationPolicyPage"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16
        Label { text: qsTr("Автопереключение"); font.pixelSize: 22; font.weight: Font.DemiBold; color: HopStyle.text }
        HopLocationPolicy {
            Layout.fillWidth: true
            Layout.fillHeight: true
            catalog: Locations
            selectionLocked: ConnectionController.isConnected || ConnectionController.isConnectionInProgress || LocationSession.running
        }
    }
}
