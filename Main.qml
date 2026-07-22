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

    ListModel {
        id: debugAudioModel
        ListElement { name: "Ля м"; hz: 220.0 }
        ListElement { name: "Ля 1"; hz: 440.0 }
        ListElement { name: "Ля 2"; hz: 880.0 }
        ListElement { name: "❌ Сброс"; hz: 0.0 }
    }

    function updateDropdowns() {
        textDropdownModel.clear()
        audioDropdownModel.clear()

        var myName = netEngine.username

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

        if (window.activeChatPeer !== "") {
            textDropdownModel.append({"name": "❌ Выйти"})
        }

        if (window.activeConferencePeers !== "") {
            audioDropdownModel.append({"name": "❌ Выход"})
        }
    }

    Connections {
        target: netEngine
        ignoreUnknownSignals: true
        function onMessageReceived(sender, text) {
            if (sender !== netEngine.username) {
                window.activeChatPeer = sender
            }
            chatModel.append({
                "senderName": sender,
                "messageText": text,
                "isMe": (sender === netEngine.username)
            })
        }
        function onIncomingCall(peer) {
            if (window.activeConferencePeers === "") {
                window.incomingCallFrom = peer
            }
        }
        function onCallAccepted() {
            window.activeConferencePeers = window.incomingCallFrom !== "" ? window.incomingCallFrom : netEngine.username
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
                spacing: 12

                // Кнопка текстового чата
                Button {
                    id: textMenuButton
                    width: 55
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
                                    var chosenName = model.name
                                    textMenu.close()
                                    if (chosenName === "❌ Выйти") {
                                        window.activeChatPeer = ""
                                    } else {
                                        window.activeChatPeer = chosenName
                                        netEngine.startChatSession(chosenName)
                                    }
                                    window.updateDropdowns()
                                }
                            }
                        }
                    }
                }

                // Кнопка аудио-звонка
                Button {
                    id: audioMenuButton
                    width: 55
                    height: 60
                    background: Rectangle {
                        color: (window.activeConferencePeers !== "" && window.incomingCallFrom === "") ? "#e74c3c" : "#2ecc71"
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
                        if (window.activeConferencePeers !== "") {
                            netEngine.stopAudioCall()
                        } else {
                            audioMenu.popup()
                        }
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
                                    var chosenName = model.name
                                    audioMenu.close()
                                    if (chosenName === "❌ Выход") {
                                        netEngine.stopAudioCall()
                                    } else {
                                        netEngine.startAudioCall(chosenName)
                                    }
                                    window.updateDropdowns()
                                }
                            }
                        }
                    }
                }

                // Панель статуса и изменения настроек
                Column {
                    width: parent.width - 140
                    height: 60
                    spacing: 4
                    justifyContent: Column.AlignVCenter

                    // Редактирование профиля (сохранение в INI при Enter)
                    Row {
                        spacing: 6
                        Text { text: "Профиль:"; color: "#a4b0be"; font.pixelSize: 12 }
                        TextInput {
                            text: netEngine.username
                            color: "white"
                            font.pixelSize: 12
                            font.bold: true
                            selectByMouse: true
                            onAccepted: {
                                netEngine.setUsername(text)
                                window.updateDropdowns()
                                focus = false
                            }
                        }
                    }

                    Text {
                        text: window.activeConferencePeers !== "" ? "🎙️ Линия: Активна" : "💤 Линия: Свободна"
                        color: window.activeConferencePeers !== "" ? "#2ecc71" : "#a4b0be"
                        font.pixelSize: 12
                    }

                    // Индикатор громкости микрофона
                    ProgressBar {
                        width: parent.width
                        height: 4
                        value: netEngine.micRms
                        background: Rectangle { color: "#34495e"; radius: 2 }
                        contentItem: Item {
                            Rectangle {
                                width: parent.parent.value * parent.parent.width
                                height: parent.parent.height
                                color: "#2ecc71"
                                radius: 2
                            }
                        }
                    }
                }
            }
        }
        // Область чата
        Rectangle {
            width: parent.width
            height: parent.height - toolbar.height
            color: "#f5f6fa"

            // Заглушка, если чат не выбран
            Text {
                text: "Выберите контакт для общения 📝"
                anchors.centerIn: parent
                color: "#7f8c8d"
                font.pixelSize: 16
                visible: window.activeChatPeer === ""
            }

            Column {
                anchors.fill: parent
                visible: window.activeChatPeer !== ""

                // Список сообщений
                ListView {
                    id: chatListView
                    width: parent.width
                    height: parent.height - 60
                    clip: true
                    model: chatModel
                    spacing: 8
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        width: chatListView.width
                        height: messageBg.height + 4

                        Rectangle {
                            id: messageBg
                            width: Math.min(messageTextElement.implicitWidth + 24, parent.width * 0.7)
                            height: messageTextElement.implicitHeight + 16
                            radius: 12
                            color: model.isMe ? "#2980b9" : "#ffffff"
                            border.color: model.isMe ? "#2980b9" : "#dcdde1"
                            anchors.right: model.isMe ? parent.right : undefined
                            anchors.left: model.isMe ? undefined : parent.left
                            anchors.margins: 8

                            Text {
                                id: messageTextElement
                                text: (model.isMe ? "" : model.senderName + ":\n") + model.messageText
                                color: model.isMe ? "white" : "#2c3e50"
                                font.pixelSize: 14
                                wrapMode: Text.Wrap
                                anchors.fill: parent
                                anchors.margins: 8
                            }
                        }
                    }

                    onCountChanged: {
                        Qt.callLater(chatListView.positionViewAtEnd)
                    }
                }

                // Поле ввода сообщений
                Rectangle {
                    width: parent.width
                    height: 60
                    color: "white"
                    border.color: "#dcdde1"

                    Row {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        TextField {
                            id: messageField
                            width: parent.width - 70
                            height: parent.height
                            placeholderText: "Напишите " + window.activeChatPeer + "..."
                            font.pixelSize: 14
                            onAccepted: {
                                if (text.trim() !== "") {
                                    netEngine.sendChatMessage(text)
                                    text = ""
                                }
                            }
                        }

                        Button {
                            width: 60
                            height: parent.height
                            text: "Отпр."
                            onClicked: {
                                if (messageField.text.trim() !== "") {
                                    netEngine.sendChatMessage(messageField.text)
                                    messageField.text = ""
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Всплывающее полноэкранное окно для Входящего Вызова
    Rectangle {
        id: incomingCallOverlay
        anchors.fill: parent
        color: "#e61a252f" // Полупрозрачный темный фон
        visible: window.incomingCallFrom !== ""
        z: 100

        // Перехват кликов по фону для отклонения вызова
        MouseArea {
            anchors.fill: parent
            onClicked: {
                netEngine.stopAudioCall()
                window.incomingCallFrom = ""
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 30
            horizontalAlignment: Text.AlignHCenter

            Text {
                text: "📞 Входящий звонок"
                color: "white"
                font.pixelSize: 26
                font.bold: true
            }

            // Аватарка вызывающего абонента
            Rectangle {
                width: 100
                height: 100
                radius: 50
                color: "#e74c3c"
                anchors.horizontalCenter: parent.horizontalCenter

                Text {
                    text: window.incomingCallFrom.charAt(0).toUpperCase()
                    color: "white"
                    font.pixelSize: 42
                    font.bold: true
                    anchors.centerIn: parent
                }
            }

            Text {
                text: window.incomingCallFrom
                color: "white"
                font.pixelSize: 22
                font.bold: true
            }

            Row {
                spacing: 40
                anchors.horizontalCenter: parent.horizontalCenter

                // Кнопка Принять звонок
                Button {
                    width: 70
                    height: 70
                    background: Rectangle { color: "#2ecc71"; radius: 35 }
                    contentItem: Text {
                        text: "✓"
                        color: "white"
                        font.pixelSize: 32
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        netEngine.acceptAudioCall()
                    }
                }

                // Кнопка Сбросить звонок
                Button {
                    width: 70
                    height: 70
                    background: Rectangle { color: "#e74c3c"; radius: 35 }
                    contentItem: Text {
                        text: "✕"
                        color: "white"
                        font.pixelSize: 32
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        netEngine.stopAudioCall()
                        window.incomingCallFrom = ""
                    }
                }
            }
        }
    }
}
