import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    visible: true
    visibility: Qt.platform.os === "windows" ? Window.Windowed : Window.FullScreen

    width: 360
    height: 640
    title: "TeleLoc Рация"

    property string activeChatPeer: ""
    property string activeConferencePeers: ""
    property int activeCallNetType: -1

    ListModel {
        id: chatLogModel
    }

    Connections {
        target: netEngine
        function onPeerListChanged() {
            if (chatMenu.opened) {
                while (chatMenu.count > 0) {
                    var item = chatMenu.takeItem(0)
                    if (item) item.destroy()
                }
                var list = netEngine.getUsers(-1 | 0)
                for (var i = 0; i < list.length; ++i) {
                    var currentName = list[i]
                    var menuItem = Qt.createQmlObject('import QtQuick.Controls; MenuItem { text: "' + currentName + '" }', chatMenu)
                    if (menuItem) {
                        chatMenu.addItem(menuItem)
                        menuItem.triggered.connect((function(name) {
                            return function() {
                                window.activeChatPeer = name
                            }
                        })(currentName))
                    }
                }
            }
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
           incomingCallDialog.callerName = peerName
            incomingCallDialog.callNetType = netType
            incomingCallDialog.open()
        }
        function onCallAccepted() {
            statusText.text = "Разговор"
        }
        function onCallStopped() {
            micIndicatorText.text = "🎤 0%"
            netIndicatorText.text = "🔊 0%"
            incomingCallDialog.close()
            callLanMenu.close()
            callApMenu.close()
            callDirectMenu.close()
            window.activeConferencePeers = ""
            window.activeCallNetType = -1
            statusText.text = "Ждём"
        }
        function onMicVolumeUpdated(volume) {
             micIndicatorText.text = "🎤 " + volume + "%"
         }
         function onNetVolumeUpdated(volume) {
             netIndicatorText.text = "🔊 " + volume + "%"
         }
         function onSetActiveNetType(_netType) {
            window.activeCallNetType = _netType
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
                    onClicked: {
                        while (chatMenu.count > 0) {
                            var item = chatMenu.takeItem(0)
                            if (item) item.destroy()
                        }
                        var users = netEngine.getUsers(-1 | 0)
                        for (var i = 0; i < users.length; ++i) {
                            var currentName = users[i]
                            var menuItem = Qt.createQmlObject('import QtQuick.Controls; MenuItem { text: "' + currentName + '" }', chatMenu)
                            if (menuItem) {
                                chatMenu.addItem(menuItem)
                                menuItem.triggered.connect((function(name) {
                                    return function() {
                                        window.activeChatPeer = name
                                    }
                                })(currentName))
                            }
                        }
                        chatMenu.open()
                    }

                    Menu {
                        id: chatMenu
                        title: "Чат с..."
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
                    onClicked: netEngine.debugUsers()
                }

                Rectangle {
                    id: micVolumeIndicator
                    width: (parent.width - 174) / 2
                    height: 50
                    color: "#34495e"
                    radius: 8
                    border.color: "#bdc3c7"
                    border.width: 1

                    Text {
                        id: micIndicatorText
                        text: "🎤 0%"
                        color: "white"
                        font.pixelSize: 11
                        font.bold: true
                        anchors.centerIn: parent
                    }
                }

                Rectangle {
                    id: netVolumeIndicator
                    width: (parent.width - 174) / 2
                    height: 50
                    color: "#34495e"
                    radius: 8
                    border.color: "#bdc3c7"
                    border.width: 1

                    Text {
                        id: netIndicatorText
                        text: "🔊 0%"
                        color: "white"
                        font.pixelSize: 11
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
                        } else if  (window.activeCallNetType === -1) {
                            while (callLanMenu.count > 0) {
                                var item = callLanMenu.takeItem(0)
                                if (item) item.destroy()
                            }
                            var users = netEngine.getUsers(0 | 0)
                            for (var i = 0; i < users.length; ++i) {
                                var currentName = users[i]

                                // Динамически создаем настоящий объект MenuItem из строки
                                var menuItem = Qt.createQmlObject('import QtQuick.Controls; MenuItem { text: "' + currentName + '" }', callLanMenu)

                                if (menuItem) {
                                    callLanMenu.addItem(menuItem) // Теперь аргументы на 100% совместимы!
                                    menuItem.triggered.connect((function(name) {
                                        return function() {
                                            window.activeConferencePeers = name
                                            window.activeCallNetType = 0
                                            netEngine.startAudioCall(name, 0 | 0)
                                        }
                                    })(currentName))
                                }
                            }
                            callLanMenu.open()
                        }
                    }

                    Menu {
                        id: callLanMenu
                        title: "LAN Вызов..."
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
                        } else  if  (window.activeCallNetType === -1){
                            while (callApMenu.count > 0) {
                                var item = callApMenu.takeItem(0)
                                if (item) item.destroy()
                            }
                            var users = netEngine.getUsers(1 | 0)
                            for (var i = 0; i < users.length; ++i) {
                                var currentName = users[i]

                                var menuItem = Qt.createQmlObject('import QtQuick.Controls; MenuItem { text: "' + currentName + '" }', callApMenu)

                                if (menuItem) {
                                    callApMenu.addItem(menuItem)
                                    menuItem.triggered.connect((function(name) {
                                        return function() {
                                            window.activeConferencePeers = name
                                            window.activeCallNetType = 1
                                            netEngine.startAudioCall(name, 1 | 0)
                                        }
                                    })(currentName))
                                }
                            }
                            callApMenu.open()
                        }
                    }

                    Menu {
                        id: callApMenu
                        title: "AP Вызов..."
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
                        } else  if  (window.activeCallNetType === -1){

                            while (callDirectMenu.count > 0) {
                                var item = callDirectMenu.takeItem(0)
                                if (item) item.destroy()
                            }
                            var users = netEngine.getUsers(2 | 0)
                            for (var i = 0; i < users.length; ++i) {
                                var currentName = users[i]

                                var menuItem = Qt.createQmlObject('import QtQuick.Controls; MenuItem { text: "' + currentName + '" }', callDirectMenu)

                                if (menuItem) {
                                    callDirectMenu.addItem(menuItem)
                                    menuItem.triggered.connect((function(name) {
                                        return function() {
                                            window.activeConferencePeers = name
                                            window.activeCallNetType = 2
                                            netEngine.startAudioCall(name, 2 | 0)
                                        }
                                    })(currentName))
                                }
                            }
                            callDirectMenu.open()
                        }
                    }

                    Menu {
                        id: callDirectMenu
                        title: "Direct Вызов..."
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

                    TextEdit {
                        id: chatText
                        text: model.text
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: 14
                        color: "#ff3e50"
                        selectByMouse: true
                        //mouseSelectionMode: Text.SelectCharacters
                        persistentSelection: true // выделение не пропадёт при потере фокуса
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
                    }
                    settingsDialog.close()
                }
            }
        }
    }
}
