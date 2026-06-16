import QtQuick
import Components

FocusScope {
    id: nowPlayingRoot

    property var navParams: ({})

    signal goBack()

    focus: true

    property string streamUrl: ""
    property string accessToken: ""

    function formatTime(ms) {
        if (ms <= 0) return "0:00"
        var totalSec = Math.floor(ms / 1000)
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        return min + ":" + (sec < 10 ? "0" : "") + sec
    }

    Connections {
        target: tidalBackend
        function onStreamUrlReady(url, token) {
            streamUrl   = url
            accessToken = token
            mpvController.loadAndPlay(url, 0, 0, -1, [], false, -1, 0.0, "", false, "", "Bearer " + token, true)
        }
    }

    Connections {
        target: mpvController
        function onPlaybackFinished(finalPos, finalDur) {
            goBack()
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            mpvController.stop()
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            mpvController.sendKey("ENTER")
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            mpvController.sendKey("LEFT")
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            mpvController.sendKey("RIGHT")
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
    }

    Column {
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.25
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.sw * 0.115625
        anchors.rightMargin: root.sw * 0.115625
        spacing: root.sh * 0.04

        Text {
            width: parent.width
            text: navParams.title || ""
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.05
            font.capitalization: Font.AllUppercase
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: navParams.artist || ""
            color: root.accentColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.04
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: navParams.album || ""
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
            elide: Text.ElideRight
        }

        Item { width: 1; height: root.sh * 0.02 }

        Rectangle {
            width: parent.width
            height: root.sh * 0.012
            color: root.surfaceColor
            radius: root.sh * 0.006

            Rectangle {
                width: mpvController.duration > 0
                    ? parent.width * (mpvController.position / mpvController.duration)
                    : 0
                height: parent.height
                color: root.accentColor
                radius: parent.radius
            }
        }

        Text {
            width: parent.width
            text: formatTime(mpvController.position) + " / " + formatTime(
                navParams.durationMs > 0 ? navParams.durationMs : mpvController.duration)
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
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
        text: "[ESC]:STOP  [◄►]:SEEK  [ENTER]:PAUSE"
    }
}
