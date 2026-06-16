import QtQuick
import Components

FocusScope {
    id: searchRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    property string query: ""
    property var trackResults: []
    property var albumResults: []
    property bool hasResults: false
    property int currentSection: 0  // 0 = tracks, 1 = albums
    property int currentTrackIndex: 0
    property int currentAlbumIndex: 0

    function currentItems() {
        return currentSection === 0 ? trackResults : albumResults
    }

    function currentIndex() {
        return currentSection === 0 ? currentTrackIndex : currentAlbumIndex
    }

    function setCurrentIndex(i) {
        if (currentSection === 0) currentTrackIndex = i
        else currentAlbumIndex = i
    }

    Connections {
        target: tidalBackend
        function onSearchResultsLoaded(tracks, albums) {
            trackResults      = tracks
            albumResults      = albums
            hasResults        = true
            currentTrackIndex = 0
            currentAlbumIndex = 0
        }
    }

    Keys.onPressed: function(event) {
        var key  = event.key
        var text = event.text

        if (key === Qt.Key_Escape) {
            if (query !== "") { query = ""; hasResults = false }
            else              { goBack() }
            event.accepted = true
            return
        }

        if (key === Qt.Key_Backspace) {
            if (hasResults && query === "") { goBack() }
            else { query = query.slice(0, -1) }
            event.accepted = true
            return
        }

        if (key === Qt.Key_Return || key === Qt.Key_Enter) {
            if (hasResults) {
                var items = currentItems()
                var idx   = currentIndex()
                if (items.length > 0 && idx < items.length) {
                    var item = items[idx]
                    if (currentSection === 0) {
                        navigateTo("NowPlaying.qml", {
                            trackId:    item.id,
                            title:      item.title,
                            artist:     item.artist,
                            album:      item.album,
                            durationMs: item.durationMs
                        }, {})
                        tidalBackend.get_stream_url(item.id)
                    } else {
                        navigateTo("Items.qml", {
                            contentType: "album_tracks",
                            sourceId:    item.id,
                            title:       item.title
                        }, {})
                    }
                }
            } else if (query.length > 0) {
                tidalBackend.search(query)
            }
            event.accepted = true
            return
        }

        if (key === Qt.Key_Up) {
            if (hasResults) {
                var idx = currentIndex()
                if (idx > 0) setCurrentIndex(idx - 1)
                else if (currentSection > 0) { currentSection-- }
            }
            event.accepted = true
            return
        }

        if (key === Qt.Key_Down) {
            if (hasResults) {
                var items = currentItems()
                var idx   = currentIndex()
                if (idx < items.length - 1) setCurrentIndex(idx + 1)
                else if (currentSection === 0 && albumResults.length > 0) { currentSection++ }
            }
            event.accepted = true
            return
        }

        if (key === Qt.Key_Left || key === Qt.Key_Right) {
            event.accepted = true
            return
        }

        if (text.length > 0 && !hasResults) {
            query += text
            event.accepted = true
        }
    }

    AppBar {
        id: appBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "SEARCH"
    }

    // Search input box
    Rectangle {
        id: searchBox
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.25
        anchors.left: parent.left
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.0583333
        color: root.surfaceColor

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: root.sw * 0.009375
            text: query.length > 0 ? query : "TYPE TO SEARCH..."
            color: query.length > 0 ? root.primaryColor : root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.05
            font.capitalization: Font.AllUppercase
            elide: Text.ElideRight
        }
    }

    // Results
    ListView {
        id: trackList
        visible: hasResults && trackResults.length > 0
        anchors.top: searchBox.bottom
        anchors.topMargin: root.sh * 0.0583333  // one row gap for section header
        anchors.left: searchBox.left
        width: searchBox.width
        height: Math.min(trackResults.length, 5) * root.sh * 0.0583333
        clip: true
        model: trackResults
        currentIndex: currentSection === 0 ? currentTrackIndex : -1
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

        header: Text {
            width: trackList.width
            height: root.sh * 0.0583333
            text: "TRACKS"
            color: currentSection === 0 ? root.accentColor : root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
            font.capitalization: Font.AllUppercase
            verticalAlignment: Text.AlignVCenter
            leftPadding: root.sw * 0.009375
        }

        delegate: Item {
            width: trackList.width
            height: root.sh * 0.0583333

            Item {
                id: tClip
                width: Math.min(tText.implicitWidth, trackList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: tText
                    visible: currentSection === 0 && trackList.currentIndex === index
                }

                Text {
                    id: tText
                    text: (modelData.title || "") + (modelData.artist ? " — " + modelData.artist : "")
                    color: currentSection === 0 && trackList.currentIndex === index
                           ? root.surfaceColor : root.primaryColor
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

                SequentialAnimation {
                    running: currentSection === 0 && trackList.currentIndex === index &&
                             tText.implicitWidth > tClip.width
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) tText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: tText; property: "x"
                        to: tClip.width - tText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: tText; property: "x"; value: 0 }
                }
            }
        }
    }

    ListView {
        id: albumList
        visible: hasResults && albumResults.length > 0
        anchors.top: trackList.visible
                     ? trackList.bottom
                     : searchBox.bottom
        anchors.topMargin: root.sh * 0.0583333
        anchors.left: searchBox.left
        width: searchBox.width
        height: Math.min(albumResults.length, 5) * root.sh * 0.0583333
        clip: true
        model: albumResults
        currentIndex: currentSection === 1 ? currentAlbumIndex : -1
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

        header: Text {
            width: albumList.width
            height: root.sh * 0.0583333
            text: "ALBUMS"
            color: currentSection === 1 ? root.accentColor : root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
            font.capitalization: Font.AllUppercase
            verticalAlignment: Text.AlignVCenter
            leftPadding: root.sw * 0.009375
        }

        delegate: Item {
            width: albumList.width
            height: root.sh * 0.0583333

            Item {
                id: aClip
                width: Math.min(aText.implicitWidth, albumList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: aText
                    visible: currentSection === 1 && albumList.currentIndex === index
                }

                Text {
                    id: aText
                    text: (modelData.title || "") + (modelData.artist ? " — " + modelData.artist : "")
                    color: currentSection === 1 && albumList.currentIndex === index
                           ? root.surfaceColor : root.primaryColor
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

                SequentialAnimation {
                    running: currentSection === 1 && albumList.currentIndex === index &&
                             aText.implicitWidth > aClip.width
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) aText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: aText; property: "x"
                        to: aClip.width - aText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: aText; property: "x"; value: 0 }
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
        text: hasResults
              ? "[ESC]:CLEAR  [▲▼]:NAVIGATE  [ENTER]:SELECT"
              : "[ESC]:BACK  [ENTER]:SEARCH"
    }
}
