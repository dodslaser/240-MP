import QtQuick
import Components

FocusScope {
    id: gamepadSettingsRoot

    signal goBack()

    property var navParams: ({})

    property var allActions: []
    property var bindingIds: []

    readonly property var stickModeOptions: ["none", "ls", "rs", "both"]
    readonly property var stickModeLabels: ({
        "none": "None",
        "ls":   "Left Stick",
        "rs":   "Right Stick",
        "both": "Both Sticks"
    })

    function buildModel() {
        var items = []

        var mode = gamepadController.stickMode()
        items.push({
            id: "stick_mode",
            label: "Stick Input",
            isStickMode: true,
            value: mode,
            valueLabel: stickModeLabels[mode] || mode
        })

        var actions = gamepadController.getActions()
        for (var i = 0; i < actions.length; i++) items.push(actions[i])

        return items
    }

    function cycleItem(index, dir) {
        var row = allActions[index]
        if (!row) return

        var updated = allActions.slice()

        if (row.isStickMode) {
            var opts = stickModeOptions
            var cur  = opts.indexOf(row.value)
            if (cur < 0) cur = 0
            var next   = (cur + dir + opts.length) % opts.length
            var newVal = opts[next]
            gamepadController.setStickMode(newVal)
            updated[index] = Object.assign({}, row, {
                value: newVal,
                valueLabel: stickModeLabels[newVal] || newVal
            })
        } else {
            var ids    = bindingIds
            var curIdx = ids.indexOf(row.binding)
            if (curIdx < 0) curIdx = 0
            var nextIdx = (curIdx + dir + ids.length) % ids.length
            var newId   = ids[nextIdx]
            gamepadController.setBinding(row.id, newId)
            updated[index] = Object.assign({}, row, {
                binding: newId,
                bindingLabel: gamepadController.bindingLabel(newId)
            })
        }

        var savedIndex = actionList.currentIndex
        allActions = updated
        actionList.currentIndex = savedIndex
        actionList.forceLayout()
    }

    Component.onCompleted: {
        bindingIds = gamepadController.availableBindingIds()
        allActions = buildModel()
    }

    // Header
    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: "Gamepad"
    }

    // Connection status indicator
    Text {
        visible: !gamepadController.connected
        text: "No gamepad detected"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0333333
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: root.sh * 0.125
        anchors.rightMargin: root.sw * 0.125
    }

    ListView {
        id: actionList
        model: gamepadSettingsRoot.allActions
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        Keys.onUpPressed:    { if (currentIndex > 0) currentIndex-- }
        Keys.onDownPressed:  { if (currentIndex < count - 1) currentIndex++ }
        Keys.onLeftPressed:  { gamepadSettingsRoot.cycleItem(currentIndex, -1) }
        Keys.onRightPressed: { gamepadSettingsRoot.cycleItem(currentIndex, 1) }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                gamepadSettingsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: actionList.width
            height: root.sh * 0.0583333

            Rectangle {
                anchors.fill: parent
                color: actionList.currentIndex === index ? root.accentColor : "transparent"

                // Left: action / setting label
                Text {
                    text: modelData.label || ""
                    color: actionList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.05
                }

                // Right: ◄ value ►
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: root.sw * 0.009375
                    spacing: root.sw * 0.00625

                    Text {
                        text: "◄"
                        color: actionList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                        font.family: root.globalFont
                        anchors.verticalCenter: parent.verticalCenter
                        topPadding: root.sh * 0.0041667
                        bottomPadding: root.sh * 0.00625
                        font.pixelSize: root.sh * 0.0375
                    }
                    Text {
                        text: modelData.isStickMode ? (modelData.valueLabel || "")
                                                    : (modelData.bindingLabel || "")
                        color: actionList.currentIndex === index ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: root.sw * 0.009375
                        rightPadding: root.sw * 0.009375
                        topPadding: root.sh * 0.0041667
                        bottomPadding: root.sh * 0.00625
                        font.pixelSize: root.sh * 0.05
                    }
                    Text {
                        text: "►"
                        color: actionList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                        font.family: root.globalFont
                        anchors.verticalCenter: parent.verticalCenter
                        topPadding: root.sh * 0.0041667
                        bottomPadding: root.sh * 0.00625
                        font.pixelSize: root.sh * 0.0375
                    }
                }
            }
        }
    }

    // Footer hint
    Text {
        text: "[ESC]:BACK [▲▼]:NAVIGATE [◄►]:CHANGE"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
