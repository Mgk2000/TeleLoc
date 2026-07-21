import QtQuick
import QtQuick.Controls

Window {
    id: window
    width: 360
    height: 640
    visible: true
    title: "TeleLoc"

    property string activeChatPeer: ""
    property string activeConferencePeers: ""
    property string incomingCallFrom: ""

    ListModel { id: chatModel }
    ListModel {
        id: contactsModel
        ListElement { name: "Иван" }
        ListElement { name: "Марья" }
        ListElement { name: "Петр" }
        ListElement { name: "Анфиса" }
    }
    ListModel { id: textDropdownModel }
    ListModel { id: audioDropdownModel }

    function updateDropdowns() {
        textDropdownModel.clear()
        audioDropdownModel.clear()

        var myName = "Пользователь"
        try {
            var saved = netEngine.getSavedName()
            if (saved && saved !== "") {
                myName = saved
            }
        } catch(e) {}

        for (var i = 0; i < contactsModel.count; i++) {
            var contactName = contactsModel.get(i).name
            if (contactName === myName) continue

            if (contactName !== window.activeChatPeer) {
                textDropdownModel.append({"name": contactName})
            }

            if (window.activeConferencePeers.indexOf(contactName) === -1) {
                audioDropdownModel.append({"name": contactName})
            }
        }

        textDropdownModel.append({"name": "Все"})
        if (window.activeChatPeer !== "") {
            textDropdownModel.append({"name": "❌ Выйти"})
        }

        audioDropdownModel.append({"name": "Все"})
        if (window.activeConferencePeers !== "") {
            audioDropdownModel.append({"name": "❌ Выйти"})
        }
    }

    Connections {
        target: netEngine
        ignoreUnknownSignals: true
        function onMessageReceived(sender, text) {
            if (sender !== netEngine.getSavedName()) {
                window.activeChatPeer = sender
            }
            chatModel.append({
                "senderName": sender,
                "messageText": text,
                "isMe": (sender === netEngine.getSavedName())
            })
        }
        function onIncomingCall(peer) {
            window.incomingCallFrom = peer
        }
        function onCallAccepted() {
            if (window.activeConferencePeers === "") {
                window.activeConferencePeers = window.incomingCallFrom
            } else {
                window.activeConferencePeers += ", " + window.incomingCallFrom
            }
            window.incomingCallFrom = ""
            window.updateDropdowns()
        }
        function onCallEnded() {
            window.activeConferencePeers = ""
            window.updateDropdowns()
        }
    }

    Component.onCompleted: {
        if (!netEngine.isRegistered()) {
            netEngine.saveNameToFile("Пользователь")
        }
        netEngine.start(netEngine.getSavedName())
        window.updateDropdowns()
    }

    Column {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            id: toolbar
            width: parent.width
            height: 90
            color: "#2c3e50"
            z: 10

            Row {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 15

                Button {
                    id: textMenuButton
                    width: 65
                    height: 60
                    contentItem: Text {
                        text: "📝"
                        font.pixelSize: 28
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        window.updateDropdowns()
                        textMenu.popup()
                    }
                    Menu {
                        id: textMenu
                        Instantiator {
                            model: textDropdownModel
                            onObjectAdded: (index, object) => textMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => textMenu.removeItem(object)
                            delegate: MenuItem {
                                text: model.name
                                onTriggered: {
                                    if (model.name === "❌ Выйти") {
                                        window.activeChatPeer = ""
                                        window.updateDropdowns()
                                    }
                                    else if (model.name !== "Все") {
                                        window.activeChatPeer = model.name
                                    }
                                    netEngine.startChatSession(model.name)
                                    window.updateDropdowns()
                                }
                            }
                        }
                    }
                }

                Button {
                    id: audioMenuButton
                    width: 65
                    height: 60
                    background: Rectangle {
                        color: window.activeConferencePeers === "" ? "#2ecc71" : "#e74c3c"
                        radius: 8
                    }
                    contentItem: Text {
                        text: "📞"
                        font.pixelSize: 28
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        window.updateDropdowns()
                        audioMenu.popup()
                    }
                    Menu {
                        id: audioMenu
                        Instantiator {
                            model: audioDropdownModel
                            onObjectAdded: (index, object) => audioMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => audioMenu.removeItem(object)
                            delegate: MenuItem {
                                text: model.name
                                onTriggered: {
                                    if (model.name === "❌ Выйти") {
                                        netEngine.stopAudioCall()
                                        window.activeConferencePeers = ""
                                        window.updateDropdowns()
                                    } else {
                                        netEngine.startAudioCall(model.name)
                                        if (window.activeConferencePeers === "") {
                                            window.activeConferencePeers = model.name
                                        } else if (model.name === "Все") {
                                            window.activeConferencePeers = "Все"
                                        } else {
                                            window.activeConferencePeers += ", " + model.name
                                        }
                                        window.updateDropdowns()
                                    }
                                }
                            }
                        }
                    }
                }

                Column {
                    width: parent.width - 175
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Text {
                        text: "Чат: " + window.activeChatPeer
                        color: "white"
                        font.bold: true
                        font.pixelSize: 15
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        text: window.activeConferencePeers === "" ? "🎙 Звонок: нет" : "🎙 В сети: " + window.activeConferencePeers
                        color: "#2ecc71"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        width: parent.width
                    }

                    Row {
                        width: parent.width
                        spacing: 10
                        visible: window.activeConferencePeers !== ""

                        Column {
                            width: (parent.width - 10) / 2
                            spacing: 2
                            Text { text: "Мик: " + netEngine.micLevel + "%"; color: "#a4b0be"; font.pixelSize: 10 }
                            ProgressBar {
                                id: micBar
                                width: parent.width
                                height: 5
                                value: netEngine.micLevel / 100.0
                                background: Rectangle { color: "#1a252f"; radius: 3 }
                                contentItem: Item {
                                    Rectangle { width: micBar.width * micBar.value; height: parent.height; color: "#3498db"; radius: 3 }
                                }
                            }
                        }

                        Column {
                            width: (parent.width - 10) / 2
                            spacing: 2
                            Text { text: "Сет: " + netEngine.netLevel + "%"; color: "#a4b0be"; font.pixelSize: 10 }
                            ProgressBar {
                                id: netBar
                                width: parent.width
                                height: 5
                                value: netEngine.netLevel / 100.0
                                background: Rectangle { color: "#1a252f"; radius: 3 }
                                contentItem: Item {
                                    Rectangle { width: netBar.width * netBar.value; height: parent.height; color: "#2ecc71"; radius: 3 }
                                }
                            }
                        }
                    }
                }
            }
        }
//-------------------part 3
        Item {
            width: parent.width
            height: parent.height - 90

            Column {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 10

                ScrollView {
                    id: chatScroll
                    width: parent.width
                    height: parent.height - 70
                    clip: true

                    ListView {
                        id: chatListView
                        model: chatModel
                        width: chatScroll.width
                        spacing: 8
                        onCountChanged: chatListView.positionViewAtEnd()

                        delegate: Item {
                            width: chatListView.width
                            height: messageBubble.height + 4

                            Rectangle {
                                id: messageBubble
                                color: model.isMe ? "#e8f5e9" : "#ffffff"
                                border.color: model.isMe ? "#c8e6c9" : "#e0e0e0"
                                border.width: 1
                                radius: 8
                                width: parent.width - 20
                                anchors.horizontalCenter: parent.horizontalCenter
                                height: msgColumn.height + 12

                                Column {
                                    id: msgColumn
                                    x: 10
                                    y: 6
                                    width: parent.width - 20
                                    spacing: 4

                                    Text {
                                        text: model.isMe ? "Вы:" : model.senderName + ":"
                                        font.bold: true
                                        font.pixelSize: 14
                                        color: model.isMe ? "#2e7d32" : "#6a1b9a"
                                    }

                                    Text {
                                        text: model.messageText
                                        color: "black"
                                        font.pixelSize: 15
                                        wrapMode: Text.Wrap
                                        width: parent.width
                                    }
                                }
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    height: 50
                    spacing: 10
                    visible: window.activeChatPeer !== ""

                    Button {
                        id: clearChatButton
                        width: 50
                        height: parent.height
                        contentItem: Text {
                            text: "🗑️"
                            font.pixelSize: 22
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: "#7f8c8d"
                            radius: 8
                        }
                        onClicked: chatModel.clear()
                    }

                    TextField {
                        id: messageInput
                        width: parent.width - 130
                        height: parent.height
                        placeholderText: "Введите сообщение..."
                        font.pixelSize: 16
                        onAccepted: okButton.clicked()
                    }

                    Button {
                        id: okButton
                        text: "▶"
                        width: 60
                        height: parent.height
                        font.pixelSize: 24
                        font.bold: true
                        background: Rectangle {
                            color: "#4caf50"
                            radius: 8
                        }
                        contentItem: Text {
                            text: okButton.text
                            font: okButton.font
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            if (messageInput.text.trim() !== "") {
                                netEngine.sendMessage(window.activeChatPeer, messageInput.text.trim())
                                messageInput.text = ""
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#aa000000"
                visible: window.incomingCallFrom !== ""
                z: 20
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        netEngine.stopAudioCall()
                        window.incomingCallFrom = ""
                    }
                }
                Rectangle {
                    width: 280
                    height: 180
                    color: "white"
                    radius: 12
                    anchors.centerIn: parent
                    MouseArea { anchors.fill: parent }
                    Column {
                        anchors.centerIn: parent
                        spacing: 20
                        width: parent.width * 0.85
                        Text {
                            text: "Входящий вызов от:\n" + window.incomingCallFrom
                            font.pixelSize: 18
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            width: parent.width
                        }
                        Button {
                            text: "📞 Ответить"
                            width: parent.width
                            height: 50
                            anchors.horizontalCenter: parent.horizontalCenter
                            background: Rectangle {
                                color: "#2ecc71"
                                radius: 8
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.pixelSize: 18
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                netEngine.acceptAudioCall(window.incomingCallFrom)
                            }
                        }
                    }
                }
            }
        }
    }
}
