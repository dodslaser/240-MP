import QtQuick
import Components

FocusScope {
    id: homeRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    property var sections: [
        { label: "MY PLAYLISTS",    contentType: "playlists" },
        { label: "FAVORITE TRACKS", contentType: "tracks" },
        { label: "FAVORITE ALBUMS", contentType: "albums" },
        { label: "MIXES",           contentType: "mixes" },
        { label: "SEARCH",          contentType: "search" }
    ]

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            if (sectionList.currentIndex > 0) sectionList.currentIndex--
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            if (sectionList.currentIndex < sections.length - 1) sectionList.currentIndex++
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            var sel = sections[sectionList.currentIndex]
            if (sel.contentType === "search") {
                navigateTo("Search.qml", {}, { currentIndex: sectionList.currentIndex })
            } else {
                navigateTo("Items.qml", { contentType: sel.contentType, title: sel.label }, { currentIndex: sectionList.currentIndex })
            }
            event.accepted = true
        }
    }

    Component.onCompleted: {
        var restore = navListState.currentIndex !== undefined ? navListState.currentIndex : 0
        sectionList.currentIndex = Math.min(restore, sections.length - 1)
    }

    AppBar {
        id: appBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    ListView {
        id: sectionList
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.25
        anchors.left: parent.left
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        model: sections

        delegate: Item {
            width: sectionList.width
            height: root.sh * 0.0583333

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, sectionList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: sectionList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.label
                    color: sectionList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.05
                }
            }
        }
    }

    Text {
        id: footer
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0333333
        text: "[ESC]:BACK  [▲▼]:NAVIGATE  [ENTER]:SELECT"
    }
}
