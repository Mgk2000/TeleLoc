import QtQuick
import QtQuick.Controls
import QtMultimedia

Window {
    id: window
    width: 360
    height: 640
    visible: true
    title: "TeleLoc (Отладка)"

    property string activeScreen: "LOGIN"
    property string activeChatPeer: ""
    property string callingPeer: ""

    // Глобальное хранилище сообщений текущей сессии
    ListModel {
        id: chatModel
    }

    // Звуковой эффект для входящих сообщений
    SoundEffect {
        id: messageSound
        source: "qrc:/sounds/message.wav"
    }

    Connections {
        target: netEngine
        ignoreUnknownSignals: true

        function onRequestOpenChat(peerName) {
            // Если запрос прилетел от кого-то другого (не от меня самого)
            if (peerName !== netEngine.getSavedName()) {
                window.activeChatPeer = peerName
                window.activeScreen = "CHAT"
                messageSound.play()
            }
        }

        function onMessageReceived(sender, text) {
            // Добавляем сообщение в модель списка с флагом авторства
            chatModel.append({
                "senderName": sender,
                "messageText": text,
                "isMe": (sender === netEngine.getSavedName())
            })
            if (sender !== netEngine.getSavedName()) {
                messageSound.play()
            }
        }

        function onIncomingCall(peer) {
            window.callingPeer = peer
            window.activeScreen = "INCOMING_CALL"
        }
        function onCallAccepted() {
            window.activeScreen = "TALKING"
        }
        function onCallEnded() {
            window.activeScreen = "MAIN"
            window.callingPeer = ""
        }
    }

    Component.onCompleted: {
        if (netEngine.isRegistered()) {
            netEngine.start(netEngine.getSavedName())
            window.activeScreen = "MAIN"
        } else {
            window.activeScreen = "LOGIN"
        }
    }

    ListModel {
        id: contactsModel
        ListElement { name: "Иван" }
        ListElement { name: "Марья" }
        ListElement { name: "Петр" }
        ListElement { name: "Анфиса" }
    }

    // 1. ЭКРАН ЛОГИНА
    Item {
        anchors.fill: parent
        visible: window.activeScreen === "LOGIN"
        Column {
            anchors.centerIn: parent
            width: parent.width * 0.85
            spacing: 20
            Text {
                text: "TeleLoc\nЛокальный чат"
                font.pixelSize: 28
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
            }
            TextField {
                id: nameInput
                width: parent.width
                placeholderText: "Введите ваше имя..."
                font.pixelSize: 18
            }
            Button {
                width: parent.width
                height: 50
                text: "Войти в сеть"
                onClicked: {
                    if (nameInput.text.trim() !== "") {
                        netEngine.saveNameToFile(nameInput.text.trim())
                        netEngine.start(nameInput.text.trim())
                        window.activeScreen = "MAIN"
                    }
                }
            }
        }
    }

    // 2. ГЛАВНЫЙ ЭКРАН (СПИСОК КОНТАКТОВ)
    Item {
        anchors.fill: parent
        visible: window.activeScreen === "MAIN"
        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15
            Button {
                text: "Сбросить регистрацию"
                anchors.right: parent.right
                onClicked: {
                    netEngine.resetRegistration()
                    window.activeScreen = "LOGIN"
                }
            }
            Text {
                text: "Список контактов:"
                font.pixelSize: 20
                font.bold: true
            }
            ListView {
                id: contactsListView
                width: parent.width
                height: parent.height - 120
                model: contactsModel
                spacing: 10
                delegate: Rectangle {
                    property bool isMe: model.name === netEngine.getSavedName()

                    width: contactsListView.width
                    height: isMe ? 0 : 60
                    visible: !isMe

                    color: "white"
                    border.color: "#dcdde1"
                    radius: 8

                    Item {
                        anchors.fill: parent
                        anchors.margins: 10
                        visible: !parent.isMe

                        Text {
                            text: model.name
                            font.pixelSize: 18
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Button {
                                text: "📝"
                                onClicked: {
                                    window.activeChatPeer = model.name
                                    netEngine.startChatSession(model.name)
                                    window.activeScreen = "CHAT" // Переключаем экран
                                }
                            }
                            Button {
                                text: "📞"
                                onClicked: {
                                    window.callingPeer = model.name
                                    window.activeScreen = "OUTGOING_CALL"
                                    netEngine.startAudioCall(model.name)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // 3. ЭКРАН ТЕКСТОВОГО ЧАТА (ЦВЕТНОЙ, С ПРОКРУТКОЙ И ОЧИСТКОЙ)
    Item {
        id: chatWindow
        anchors.fill: parent
        visible: window.activeScreen === "CHAT"

        Column {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 10

            // Верхняя панель управления чата
            Row {
                width: parent.width
                height: 40
                spacing: 12

                Button {
                    text: "⬅ Назад"
                    width: 80
                    height: 35
                    onClicked: window.activeScreen = "MAIN" // Чистый возврат к списку
                }

                Text {
                    text: "Чат с: " + window.activeChatPeer
                    font.bold: true
                    font.pixelSize: 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 180
                    elide: Text.ElideRight
                }

                Button {
                    text: "Очистить"
                    width: 80
                    height: 35
                    onClicked: chatModel.clear()
                }
            }

            // Окно сообщений с прокруткой (ListView)
            ScrollView {
                id: chatScroll
                width: parent.width
                height: parent.height - 110
                clip: true

                ListView {
                    id: chatListView
                    model: chatModel
                    width: chatScroll.width
                    spacing: 8
                    // Принудительно удерживаем фокус внизу при добавлении сообщений
                    onCountChanged: chatListView.positionViewAtEnd()

                    delegate: Item {
                        width: chatListView.width
                        height: messageBubble.height + 4

                        Rectangle {
                            id: messageBubble
                            // Подложка: светло-зеленая для "Вы", белая для собеседника
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
                                    // Меняем никнейм на "Вы:", если сообщение моё
                                    text: model.isMe ? "Вы:" : model.senderName + ":"
                                    font.bold: true
                                    font.pixelSize: 14
                                    // Автор — зелёный, собеседник — фиолетовый
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

            // Нижняя панель: Поле ввода и кнопка ОК
            Row {
                width: parent.width
                height: 40
                spacing: 10

                TextField {
                    id: messageInput
                    width: parent.width - 80
                    placeholderText: "Введите сообщение..."
                    font.pixelSize: 16
                    onAccepted: okButton.clicked() // Отправка по клавише Enter
                }

                Button {
                    id: okButton
                    text: "▶"
                    width: 70
                    height: parent.height
                    onClicked: {
                        if (messageInput.text.trim() !== "") {
                            netEngine.sendMessage(window.activeChatPeer, messageInput.text.trim())
                            messageInput.text = ""
                        }
                    }
                }
            }
        }
    }

    // 4. ЭКРАН ИСХОДЯЩЕГО ВЫЗОВА
    Item {
        anchors.fill: parent
        visible: window.activeScreen === "OUTGOING_CALL"
        Column {
            anchors.centerIn: parent
            spacing: 30
            Text {
                text: "Вызов: " + window.callingPeer
                font.pixelSize: 24
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                text: "Ожидание ответа..."
                font.pixelSize: 18
                color: "#7f8c8d"
            }
            Button {
                text: "Отмена"
                width: 150
                height: 50
                onClicked: netEngine.stopAudioCall()
            }
        }
    }

    // 5. ЭКРАН ВХОДЯЩЕГО ВЫЗОВА
    Item {
        anchors.fill: parent
        visible: window.activeScreen === "INCOMING_CALL"
        Column {
            anchors.centerIn: parent
            spacing: 30
            Text {
                text: "Входящий звонок"
                font.pixelSize: 24
                color: "#e74c3c"
                font.bold: true
            }
            Text {
                text: window.callingPeer
                font.pixelSize: 22
                font.bold: true
            }
            Row {
                spacing: 20
                Button {
                    text: "Ответить"
                    width: 120
                    height: 50
                    onClicked: netEngine.startAudioCall(window.callingPeer)
                }
                Button {
                    text: "Сбросить"
                    width: 120
                    height: 50
                    onClicked: netEngine.stopAudioCall()
                }
            }
        }
    }

    // 6. ЭКРАН АКТИВНОГО РАЗГОВОРА (ТЕЛЕФОН С ИНДИКАТОРАМИ ГРОМКОСТИ)
    Item {
        anchors.fill: parent
        visible: window.activeScreen === "TALKING"

        Column {
            anchors.centerIn: parent
            spacing: 30
            width: parent.width * 0.85

            Text {
                text: "Разговор с: " + window.callingPeer
                font.pixelSize: 24
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Rectangle {
                width: 100
                height: 100
                radius: 50
                color: "#2ecc71"
                anchors.horizontalCenter: parent.horizontalCenter
                Text {
                    text: "🎙"
                    font.pixelSize: 40
                    anchors.centerIn: parent
                }
            }

            // Блок отладочных VU-метров громкости
            Column {
                width: parent.width
                spacing: 12
                anchors.horizontalCenter: parent.horizontalCenter

                // 1. Индикатор микрофона
                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: "Микрофон (исходящий): " + netEngine.micLevel + "%"
                        font.pixelSize: 13
                        color: "#7f8c8d"
                    }
                    ProgressBar {
                        id: micBar
                        width: parent.width
                        value: netEngine.micLevel / 100.0
                        background: Rectangle { color: "#eee"; radius: 4; height: 10 }
                        contentItem: Item {
                            Rectangle {
                                width: micBar.width * micBar.value
                                height: 10
                                color: "#3498db"
                                radius: 4
                            }
                        }
                    }
                }

                // 2. Индикатор сети
                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: "Сеть (входящий звук): " + netEngine.netLevel + "%"
                        font.pixelSize: 13
                        color: "#7f8c8d"
                    }
                    ProgressBar {
                        id: netBar
                        width: parent.width
                        value: netEngine.netLevel / 100.0
                        background: Rectangle { color: "#eee"; radius: 4; height: 10 }
                        contentItem: Item {
                            Rectangle {
                                width: netBar.width * netBar.value
                                height: 10
                                color: "#2ecc71"
                                radius: 4
                            }
                        }
                    }
                }
            }

            // Кнопка отбоя
            Button {
                text: "Завершить"
                width: 180
                height: 50
                anchors.horizontalCenter: parent.horizontalCenter
                background: Rectangle {
                    color: "#e74c3c"
                    radius: 6
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pixelSize: 16
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: netEngine.stopAudioCall()
            }
        }
    }
}
