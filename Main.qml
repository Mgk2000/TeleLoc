import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 360
    height: 640
    title: "TeleLoc Рация"

    property string activeChatPeer: ""
    property string activeConferencePeers: ""
    property int activeCallNetType: -1

    property var textDropdownArray: []
    property var audioDropdownLanArray: []
    property var audioDropdownApArray: []
    property var audioDropdownDirectArray: []

    ListModel {
        id: chatLogModel
    }
    signal requestLanCall(string peerName)
    signal requestApCall(string peerName)
    signal requestDirectCall(string peerName)

    function updateDropdowns() {
        var textTmp = []
        var lanTmp = []
        var apTmp = []
        var directTmp = []
        var myName = "Пользователь"

        try {
            var saved = netEngine.getSavedName()
            if (saved && saved !== "") myName = saved
        } catch(e) {}

        var uniqueChatPeers = {}
        var netTypes = [0, 1, 2]
        var totalPeersCount = 0

        for (var n = 0; n < netTypes.length; n++) {
            var users = netEngine.getUsers(netTypes[n])
            for (var u = 0; u < users.length; u++) {
                if (users[u].name !== myName) {
                    if (!uniqueChatPeers[users[u].name]) {
                        uniqueChatPeers[users[u].name] = true
                        totalPeersCount++
                    }
                }
            }
        }

        for (var peer in uniqueChatPeers) {
            if (peer !== window.activeChatPeer) {
                textTmp.push(peer)
            }
        }

        if (totalPeersCount > 1) {
            textTmp.push("Все")
        }

        var lanUsers = netEngine.getUsers(0)
        for (var i = 0; i < lanUsers.length; i++) {
            if (lanUsers[i].name !== myName && window.activeConferencePeers.indexOf(lanUsers[i].name) === -1) lanTmp.push(lanUsers[i].name)
        }

        var apUsers = netEngine.getUsers(1)
        for (var j = 0; j < apUsers.length; j++) {
            if (apUsers[j].name !== myName && window.activeConferencePeers.indexOf(apUsers[j].name) === -1) apTmp.push(apUsers[j].name)
        }

        var directUsers = netEngine.getUsers(2)
        for (var k = 0; k < directUsers.length; k++) {
            if (directUsers[k].name !== myName && window.activeConferencePeers.indexOf(directUsers[k].name) === -1) directTmp.push(directUsers[k].name)
        }

        if (window.activeChatPeer !== "") textTmp.push("❌ Выйти")

        window.textDropdownArray = textTmp
        window.audioDropdownLanArray = lanTmp
        window.audioDropdownApArray = apTmp
        window.audioDropdownDirectArray = directTmp
    }
    function executeAudioCall(peerName, netType) {
        netEngine.startAudioCall(peerName, netType)
    }

    function makeCall(peerName, netType) {
        netEngine.startAudioCall(peerName, netType)
    }

    function dropCall() {
        netEngine.stopAudioCall()
    }

    Connections {
        target: netEngine
        function onPeerListChanged() {
            window.updateDropdowns()

        }
        function onMessageReceived(fromIp, message) {
            var currentTime = new Date().toLocaleTimeString(Qt.locale(), "hh:mm")
            chatLogModel.append({
                "sender": fromIp,
                "text": message,
                "time": currentTime
            })
        }
        function onIncomingCall(peerName, netType) {
            if (window.activeCallNetType !== -1) {
                netEngine.stopAudioCall()
                return
            }
            incomingCallDialog.callerName = peerName
            incomingCallDialog.callNetType = netType
            incomingCallDialog.open()
        }
        function onCallAccepted() {
            statusText.text = "Разговор"
            window.updateDropdowns()
        }
        function onCallStopped() {
            callLanMenu.close()
            callApMenu.close()
            callDirectMenu.close()
            window.activeConferencePeers = ""
            window.activeCallNetType = -1
            statusText.text = "Ждём"
            window.updateDropdowns()
        }
        Component.onCompleted: {
            window.requestLanCall.connect(function(name) { netEngine.startAudioCall(name, 0) })
            window.requestApCall.connect(function(name) { netEngine.startAudioCall(name, 1) })
            window.requestDirectCall.connect(function(name) { netEngine.startAudioCall(name, 2) })
        }
    }

    Rectangle {
        id: toolbar
        width: parent.width
        height: 115
        color: "#2c3e50"
        anchors.top: parent.top

        Column {
            anchors.fill: parent
            anchors.margins: 5
            spacing: 5

            Row {
                width: parent.width
                height: 50
                spacing: 8

                Button {
                    id: chatMenuButton
                    width: 50
                    height: 50
                    background: Rectangle {
                        color: window.activeChatPeer !== "" ? "#e74c3c" : "#3498db"
                        radius: 8
                    }
                    contentItem: Text {
                        text: "📝"
                        font.pixelSize: 20
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: chatMenu.open()

                    Menu {
                        id: chatMenu
                        title: "Чат с..."
                        Instantiator {
                            model: window.textDropdownArray
                            onObjectAdded: (index, object) => chatMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => chatMenu.removeItem(object)
                            delegate: Column {
                                width: parent ? parent.width : 0

                                MenuSeparator {
                                    width: parent.width
                                    visible: modelData === "Все" || modelData === "❌ Выйти"
                                }

                                MenuItem {
                                    width: parent.width
                                    text: modelData
                                    onTriggered: {
                                        var choice = modelData
                                        chatMenu.close()
                                        if (choice === "❌ Выйти") {
                                            window.activeChatPeer = ""
                                        } else {
                                            window.activeChatPeer = choice
                                        }
                                        window.updateDropdowns()
                                    }
                                }
                            }
                        }
                    }
                 }

                Button {
                    id: settingsButton
                    width: 50
                    height: 50
                    background: Rectangle {
                        color: "#7f8c8d"
                        radius: 8
                    }
                    contentItem: Text {
                        text: "⚙️"
                        font.pixelSize: 20
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: settingsDialog.open()
                }

                Button {
                    id: debugToneButton
                    width: 50
                    height: 50
                    background: Rectangle {
                        color: "#f1c40f"
                        radius: 8
                    }
                    contentItem: Text {
                        text: "🎵"
                        font.pixelSize: 20
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        netEngine.debugUsers()
                    }
                }

                Rectangle {
                    id: volumeIndicator
                    width: parent.width - 174
                    height: 50
                    color: "#34495e"
                    radius: 8
                    border.color: "#bdc3c7"
                    border.width: 1

                    Text {
                        id: volumeIndicatorText
                        text: "🎤 0%"
                        color: "white"
                        font.pixelSize: 12
                        font.bold: true
                        anchors.centerIn: parent
                    }
                }
            }
            Row {
                width: parent.width
                height: 50
                spacing: 8

                Button {
                    id: callLanButton
                    width: 50
                    height: 50
                    background: Rectangle {
                        color: window.activeCallNetType === 0 ? "#e74c3c" : "#2ecc71"
                        radius: 8
                    }
                    contentItem: Text {
                        text: "🏠📞"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (window.activeCallNetType === 0) {
                            netEngine.stopAudioCall()
                        } else {
                            callLanMenu.open()
                        }
                    }

                    Menu {
                        id: callLanMenu
                        title: "LAN Вызов..."
                        Instantiator {
                            model: window.audioDropdownLanArray
                            onObjectAdded: (index, object) => callLanMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => callLanMenu.removeItem(object)
                            delegate: MenuItem {
                                text: modelData
                                onTriggered: {
                                    var name = modelData
                                    var engineLink = netEngine
                                    callLanMenu.close()
                                    window.activeConferencePeers = name
                                    window.activeCallNetType = 0
                                    window.updateDropdowns()
                                    engineLink.startAudioCall(name, 0)
                                }
                            }
                        }
                    }
                }

                Button {
                    id: callApButton
                    width: 50
                    height: 50
                    background: Rectangle {
                        color: window.activeCallNetType === 1 ? "#e74c3c" : "#e67e22"
                        radius: 8
                    }
                    contentItem: Text {
                        text: "📱📞"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (window.activeCallNetType === 1) {
                            netEngine.stopAudioCall()
                        } else {
                            callApMenu.open()
                        }
                    }

                    Menu {
                        id: callApMenu
                        title: "AP Вызов..."
                        Instantiator {
                            model: window.audioDropdownApArray
                            onObjectAdded: (index, object) => callApMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => callApMenu.removeItem(object)
                            delegate: MenuItem {
                                text: modelData
                                onTriggered: {
                                    var name = modelData
                                    var engineLink = netEngine
                                    callApMenu.close()
                                    window.activeConferencePeers = name
                                    window.activeCallNetType = 1
                                    window.updateDropdowns()
                                    engineLink.startAudioCall(name, 1)
                                }
                            }
                        }
                    }
                }

                Button {
                    id: callDirectButton
                    width: 50
                    height: 50
                    background: Rectangle {
                        color: window.activeCallNetType === 2 ? "#e74c3c" : "#2980b9"
                        radius: 8
                    }
                    contentItem: Text {
                        text: "⚡📞"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (window.activeCallNetType === 2) {
                            netEngine.stopAudioCall()
                        } else {
                            callDirectMenu.open()
                        }
                    }

                    Menu {
                        id: callDirectMenu
                        title: "Direct Вызов..."
                        Instantiator {
                            model: window.audioDropdownDirectArray
                            onObjectAdded: (index, object) => callDirectMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => callDirectMenu.removeItem(object)
                            delegate: MenuItem {
                                text: modelData
                                onTriggered: {
                                    var name = modelData
                                    var engineLink = netEngine
                                    callDirectMenu.close()
                                    window.activeConferencePeers = name
                                    window.activeCallNetType = 2
                                    window.updateDropdowns()
                                    engineLink.startAudioCall(name, 2)
                                }
                            }
                        }
                    }
                }

                Button {
                    id: callBluetoothButton
                    width: 50
                    height: 50
                    background: Rectangle {
                        color: "#8e44ad"
                        radius: 8
                    }
                    contentItem: Text {
                        text: "🌐"
                        font.pixelSize: 20
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text {
                    id: statusText
                    width: parent.width - 242
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    text: window.activeChatPeer !== "" ? window.activeChatPeer : (window.activeConferencePeers !== "" ? "Звонок" : "Ждём")
                    color: "white"
                    font.bold: true
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }

    ListView {
        id: chatListView
        width: parent.width
        anchors.top: toolbar.bottom
        anchors.bottom: messageInputRow.top
        anchors.margins: 10
        model: chatLogModel
        clip: true
        delegate: Item {
            width: chatListView.width
            height: chatText.implicitHeight + 30

            Rectangle {
                width: parent.width * 0.75
                height: chatText.implicitHeight + 20
                color: model.sender === netEngine.getSavedName() ? "#a3e4d7" : "#eaecee"
                radius: 10
                anchors.right: model.sender === netEngine.getSavedName() ? parent.right : undefined
                anchors.left: model.sender === netEngine.getSavedName() ? undefined : parent.left

                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Text {
                        text: model.sender
                        font.bold: true
                        font.pixelSize: 11
                        color: "#7f8c8d"
                    }

                    Text {
                        id: chatText
                        text: model.text
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: 14
                        color: "#2c3e50"
                    }

                    Text {
                        text: model.time
                        font.pixelSize: 10
                        color: "#95a5a6"
                        anchors.right: parent.right
                    }
                }
            }
        }
    }

    Row {
        id: messageInputRow
        width: parent.width
        height: 50
        spacing: 5
        anchors.bottom: parent.bottom
        anchors.margins: 5
        visible: window.activeChatPeer !== ""

        TextField {
            id: messageField
            width: parent.width - 65
            height: 45
            placeholderText: "Сообщение для " + window.activeChatPeer + "..."
            font.pixelSize: 14
            background: Rectangle {
                border.color: "#bdc3c7"
                radius: 6
            }
        }

        Button {
            width: 55
            height: 45
            background: Rectangle { color: "#3498db"; radius: 6 }
            contentItem: Text { text: "➔"; color: "white"; font.bold: true; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            onClicked: {
                if (messageField.text.trim() !== "") {
                    netEngine.sendMessage(window.activeChatPeer, messageField.text)
                    var currentTime = new Date().toLocaleTimeString(Qt.locale(), "hh:mm")
                    chatLogModel.append({
                        "sender": netEngine.getSavedName(),
                        "text": messageField.text,
                        "time": currentTime
                    })
                    messageField.text = ""
                    chatListView.positionViewAtEnd()
                }
            }
        }
    }

    Dialog {
        id: incomingCallDialog
        title: "Входящий вызов"
        anchors.centerIn: parent
        width: 300
        height: 180
        modal: true
        closePolicy: Dialog.NoAutoClose

        property string callerName: ""
        property int callNetType: -1

        background: Rectangle {
            color: "#e74c3c"
            radius: 12
        }

        contentItem: Column {
            anchors.centerIn: parent
            spacing: 20
            width: parent.width * 0.9

            Text {
                text: incomingCallDialog.callerName + " вызывает вас..."
                color: "white"
                font.pixelSize: 18
                font.bold: true
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Row {
                spacing: 15
                anchors.horizontalCenter: parent.horizontalCenter

                Button {
                    width: 100
                    height: 45
                    background: Rectangle { color: "#2ecc71"; radius: 6 }
                    contentItem: Text { text: "Принять"; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        incomingCallDialog.close()
                        window.activeConferencePeers = incomingCallDialog.callerName
                        window.activeCallNetType = incomingCallDialog.callNetType
                        window.updateDropdowns()
                        netEngine.acceptAudioCall(incomingCallDialog.callerName, incomingCallDialog.callNetType)
                    }
                }

                Button {
                    width: 100
                    height: 45
                    background: Rectangle { color: "#95a5a6"; radius: 6 }
                    contentItem: Text { text: "Сброс"; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        incomingCallDialog.close()
                        netEngine.stopAudioCall()
                    }
                }
            }
        }
    }

    Dialog {
        id: settingsDialog
        title: "Настройки"
        anchors.centerIn: parent
        width: 280
        height: 180
        modal: true

        contentItem: Column {
            anchors.centerIn: parent
            spacing: 15
            width: parent.width * 0.9

            TextField {
                id: settingsNameInput
                width: parent.width
                placeholderText: "Ваше имя"
                text: netEngine.getSavedName()
            }

            Button {
                width: parent.width
                height: 40
                background: Rectangle { color: "#2ecc71"; radius: 6 }
                contentItem: Text { text: "Сохранить"; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: {
                    if (settingsNameInput.text.trim() !== "") {
                        netEngine.saveNameToFile(settingsNameInput.text)
                        window.updateDropdowns()
                    }
                    settingsDialog.close()
                }
            }
        }
    }
}
