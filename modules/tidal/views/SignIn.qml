import QtQuick
import Components

FocusScope {
    id: signInRoot

    property var navParams: ({})

    signal goBack()

    focus: true

    property string userCode: ""
    property string verificationUri: ""
    property string qrData: ""
    property int    qrSize: 0
    property bool   waitingForAuth: false
    property string errorMessage: ""

    Connections {
        target: tidalBackend
        function onDeviceAuthReady(code, uri, qr, size) {
            userCode        = code
            verificationUri = uri
            qrData          = qr
            qrSize          = size
            waitingForAuth  = true
            errorMessage    = ""
        }
        function onErrorOccurred(msg) {
            errorMessage   = msg
            waitingForAuth = false
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (!waitingForAuth) {
                errorMessage = ""
                tidalBackend.start_auth()
            }
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

    // ── Idle state ───────────────────────────────────────────────────────────
    Column {
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.25
        anchors.left: parent.left
        anchors.leftMargin: root.sw * 0.115625
        anchors.right: parent.right
        anchors.rightMargin: root.sw * 0.115625
        spacing: root.sh * 0.05
        visible: !waitingForAuth && errorMessage === ""

        Text {
            width: parent.width
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.05
            font.capitalization: Font.AllUppercase
            text: "Sign in with Tidal"
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
            text: "Press ENTER to start. A short code will appear that you enter at tidal.com."
        }
    }

    // ── Device-code auth state ───────────────────────────────────────────────
    Row {
        id: authRow
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.25
        anchors.left: parent.left
        anchors.leftMargin: root.sw * 0.115625
        anchors.right: parent.right
        anchors.rightMargin: root.sw * 0.115625
        spacing: root.sw * 0.06
        visible: waitingForAuth

        Column {
            width: qrSize > 0 ? parent.width * 0.55 : parent.width
            spacing: root.sh * 0.04

            Text {
                width: parent.width
                color: root.secondaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
                font.capitalization: Font.AllUppercase
                text: "Go to:"
            }

            Text {
                width: parent.width
                color: root.accentColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.04
                text: verificationUri
            }

            Text {
                width: parent.width
                color: root.secondaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
                font.capitalization: Font.AllUppercase
                text: "Enter code:"
            }

            // Prominent code display
            Rectangle {
                width: parent.width
                height: codeText.implicitHeight + root.sh * 0.04
                color: root.surfaceColor
                radius: root.sh * 0.01

                Text {
                    id: codeText
                    anchors.centerIn: parent
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.075
                    font.bold: true
                    font.letterSpacing: root.sw * 0.008
                    text: userCode
                }
            }

            Text {
                width: parent.width
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
                text: "Waiting for authorization…"
            }
        }

        // QR code (rendered only when qrSize > 0)
        Item {
            width: parent.width * 0.35
            height: width
            visible: qrSize > 0

            Rectangle {
                anchors.fill: parent
                color: "white"
                radius: root.sh * 0.008
            }

            Grid {
                id: qrGrid
                anchors.centerIn: parent
                columns: qrSize
                rows: qrSize
                Repeater {
                    model: qrData.length
                    Rectangle {
                        required property int index
                        width:  (qrGrid.parent.width  - root.sh * 0.03) / qrSize
                        height: width
                        color: qrData.charAt(index) === "1" ? "black" : "white"
                    }
                }
            }
        }
    }

    // ── Error state ──────────────────────────────────────────────────────────
    Column {
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.25
        anchors.left: parent.left
        anchors.leftMargin: root.sw * 0.115625
        anchors.right: parent.right
        anchors.rightMargin: root.sw * 0.115625
        spacing: root.sh * 0.05
        visible: !waitingForAuth && errorMessage !== ""

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: "#ff6666"
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
            text: errorMessage
        }
        Text {
            width: parent.width
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333
            text: "Press ENTER to try again."
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
        text: waitingForAuth ? "[ESC]:BACK" : "[ESC]:BACK  [ENTER]:SIGN IN"
    }
}
