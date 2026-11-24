import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
import "../Controls2"
import "../Components"
PageType {
    objectName: "hopLocationPolicyPage"
    ScrollView {
        id: policyScroll
        anchors.fill: parent
        anchors.margins: HopStyle.mobile ? 16 : 20
        anchors.topMargin: HopStyle.mobile ? 4 : 20
        contentWidth: availableWidth
        clip: true
    ColumnLayout {
        width: policyScroll.availableWidth
        height: HopStyle.mobile ? implicitHeight : Math.max(implicitHeight, policyScroll.availableHeight)
        spacing: HopStyle.mobile ? 10 : 16
        Label { visible: !HopStyle.mobile; text: qsTr("Автопереключение"); font.pixelSize: 22; font.weight: Font.DemiBold; color: HopStyle.text }
        HopLocationPolicy {
            Layout.fillWidth: true
            Layout.fillHeight: !HopStyle.mobile
            catalog: Locations
            selectionLocked: ConnectionController.isConnected || ConnectionController.isConnectionInProgress || LocationSession.running
        }
    }
    }
}
