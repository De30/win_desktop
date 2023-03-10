import QtQml 2.15
import QtQuick 2.15
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.2
import Style 1.0

ColumnLayout {
    id: unifiedSearchResultNothingFoundContainer

    required property string text

    spacing: Style.standardSpacing
    anchors.leftMargin: Style.unifiedSearchResultNothingFoundHorizontalMargin
    anchors.rightMargin: Style.unifiedSearchResultNothingFoundHorizontalMargin

    Image {
        id: unifiedSearchResultsNoResultsLabelIcon
        source: "qrc:///client/theme/magnifying-glass.svg"
        sourceSize.width: Style.trayWindowHeaderHeight / 2
        sourceSize.height: Style.trayWindowHeaderHeight / 2
        Layout.alignment: Qt.AlignHCenter
    }

    EnforcedPlainTextLabel {
        id: unifiedSearchResultsNoResultsLabel
        text: qsTr("No results for")
        color: Style.menuBorder
        font.pixelSize: Style.subLinePixelSize * 1.25
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.preferredHeight: Style.trayWindowHeaderHeight / 2
        horizontalAlignment: Text.AlignHCenter
    }

    EnforcedPlainTextLabel {
        id: unifiedSearchResultsNoResultsLabelDetails
        text: unifiedSearchResultNothingFoundContainer.text
        color: Style.ncTextColor
        font.pixelSize: Style.topLinePixelSize * 1.25
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        Layout.fillWidth: true
        Layout.preferredHeight: Style.trayWindowHeaderHeight / 2
        horizontalAlignment: Text.AlignHCenter
    }
}
