import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    property bool loading: true
    property var items: []

    function loadContent() {
        var ct = navParams.contentType || ""
        var sid = navParams.sourceId || ""
        if (ct === "playlists")         tidalBackend.load_playlists()
        else if (ct === "tracks")       tidalBackend.load_favorite_tracks()
        else if (ct === "albums")       tidalBackend.load_favorite_albums()
        else if (ct === "mixes")        tidalBackend.load_mixes()
        else if (ct === "playlist_tracks") tidalBackend.load_playlist_tracks(sid)
        else if (ct === "album_tracks") tidalBackend.load_album_tracks(sid)
        else if (ct === "mix_tracks")   tidalBackend.load_mix_tracks(sid)
    }

    function itemLabel(item) {
        var ct = navParams.contentType || ""
        if (ct === "playlists" || ct === "mixes") {
            return item.title || ""
        }
        var s = item.title || ""
        if (item.artist) s += " — " + item.artist
        return s
    }

    function onItemSelected(item) {
        var ct = navParams.contentType || ""
        if (ct === "playlists") {
            navigateTo("Items.qml", { contentType: "playlist_tracks", sourceId: item.id, title: item.title }, { currentIndex: itemList.currentIndex })
        } else if (ct === "albums") {
            navigateTo("Items.qml", { contentType: "album_tracks", sourceId: item.id, title: item.title }, { currentIndex: itemList.currentIndex })
        } else if (ct === "mixes") {
            navigateTo("Items.qml", { contentType: "mix_tracks", sourceId: item.id, title: item.title }, { currentIndex: itemList.currentIndex })
        } else {
            navigateTo("NowPlaying.qml", {
                trackId:    item.id,
                title:      item.title,
                artist:     item.artist,
                album:      item.album,
                durationMs: item.durationMs
            }, { currentIndex: itemList.currentIndex })
            tidalBackend.get_stream_url(item.id)
        }
    }

    Connections {
        target: tidalBackend
        function onTracksLoaded(data)   { loading = false; items = data }
        function onPlaylistsLoaded(data){ loading = false; items = data }
        function onAlbumsLoaded(data)   { loading = false; items = data }
        function onMixesLoaded(data)    { loading = false; items = data }
        function onErrorOccurred(msg)   { loading = false }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            if (itemList.currentIndex > 0) itemList.currentIndex--
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            if (itemList.currentIndex < items.length - 1) itemList.currentIndex++
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (items.length > 0)
                onItemSelected(items[itemList.currentIndex])
            event.accepted = true
        }
    }

    Component.onCompleted: {
        loadContent()
        var restore = navListState.currentIndex !== undefined ? navListState.currentIndex : 0
        itemList.currentIndex = Math.min(restore, Math.max(0, items.length - 1))
    }

    AppBar {
        id: appBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: navParams.title || navParams.contentType || ""
    }

    Text {
        anchors.centerIn: parent
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.05
        visible: loading
    }

    Text {
        anchors.centerIn: parent
        text: "NO ITEMS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.05
        visible: !loading && items.length === 0
    }

    ListView {
        id: itemList
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.25
        anchors.left: parent.left
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        model: items
        visible: !loading && items.length > 0

        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.0583333

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, itemList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: itemList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: itemsRoot.itemLabel(modelData)
                    color: itemList.currentIndex === index ? root.surfaceColor : root.primaryColor
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
                    running: (itemList.currentIndex === index) &&
                             (rowText.implicitWidth > textClip.width)
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) rowText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: rowText; property: "x"
                        to: textClip.width - rowText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: rowText; property: "x"; value: 0 }
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
