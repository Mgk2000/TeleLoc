import QtQuick
import QtQuick.Controls

Window {
    id: window
    width: 360
    height: 640
    visible: true
    title: "TeleLoc"
    color: "#f5f6fa"

    property real currentVolume: 0.0
    property string activeScreen: "MAIN"

    Connections {
        target: _audioEngine
        ignoreUnknownSignals: true
        function onMicVolumeChanged() {
            window.currentVolume = _audioEngine.micVolume
        }
    }

    Connections {
        target: _networkEngine
        ignoreUnknownSignals: true
        function onRequestOpenChat(fromPeer) {
            window.activeScreen = "CHAT"
        }
    }

    Component.onCompleted: {
        if (!_networkEngine.isRegistered) {
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

    Item {
        anchors.fill: parent
        visible: window.activeScreen === "LOGIN" && _networkEngine.callStatus === "IDLE"

        Column {
            anchors.centerIn: parent
            width: parent.width * 0.85
            spacing: 25

            Text {
                text: "Первый запуск\nВведите ваше имя"
                font.pixelSize: 32
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
                color: "#2f3640"
            }

            TextField {
                id: nameInput
                width: parent.width
                placeholderText: "Имя..."
                font.pixelSize: 22
                background: Rectangle { implicitHeight: 60; radius: 10; border.color: "#7f8fa6" }
            }

            Button {
                width: parent.width
                height: 60
                text: "Зарегистрировать гаджет"
                contentItem: Text { text: parent.text; font.pixelSize: 22; font.bold: true; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: "#00a8ff"; radius: 10 }
                onClicked: {
                    if (nameInput.text.trim() !== "") {
                        _networkEngine.saveNameToFile(nameInput.text.trim())
                        window.activeScreen = "MAIN"
                    }
                }
            }
        }
    }

    Item {
        anchors.fill: parent
        visible: window.activeScreen === "MAIN" && _networkEngine.callStatus === "IDLE"

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 20

            Button {
                text: "Сбросить регистрацию"
                anchors.right: parent.right
                onClicked: {
                    _networkEngine.resetRegistration()
                    window.activeScreen = "LOGIN"
                }
            }

            ListView {
                width: parent.width
                height: parent.height - 150
                model: contactsModel
                spacing: 12

                delegate: Rectangle {
                    width: parent.width
                    visible: model.name !== _networkEngine.myName
                    height: model.name !== _networkEngine.myName ? 80 : 0
                    radius: 12
                    color: "white"
                    border.color: "#dcdde1"
                    border.width: 1

                    Item {
                        anchors.fill: parent
                        anchors.margins: 15

                        Text {
                            text: model.name
                            font.pixelSize: 24
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                            color: "#2f3640"
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            Button {
                                text: "📝"
                                contentItem: Text { text: parent.text; font.pixelSize: 26 }
                                background: Rectangle { implicitWidth: 60; implicitHeight: 55; color: "#eccc68"; radius: 8 }
                                onClicked: {
                                    _networkEngine.activeChatPeer = model.name
                                    window.activeScreen = "CHAT"
                                }
                            }

                            Button {
                                text: "📞"
                                contentItem: Text { text: parent.text; font.pixelSize: 26 }
                                background: Rectangle { implicitWidth: 60; implicitHeight: 55; color: "#4cd137"; radius: 8 }
                                onClicked: { _networkEngine.startCall(model.name) }
                            }
                        }
                    }
                }
            }
        }
    }
    Item {
        anchors.fill: parent
        visible: window.activeScreen === "CHAT" && _networkEngine.callStatus === "IDLE"

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            Row {
                width: parent.width
                spacing: 10

                Button {
                    text: "⬅ Назад"
                    font.pixelSize: 16
                    onClicked: { window.activeScreen = "MAIN" }
                }

                Text {
                    text: "Чат: " + _networkEngine.activeChatPeer
                    font.pixelSize: 20
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }

                Item { implicitWidth: 10; height: 1 }

                Button {
                    text: "🗑️ Чистить"
                    font.pixelSize: 14
                    onClicked: { _networkEngine.clearChatHistory(_networkEngine.activeChatPeer) }
                }
            }

            ScrollView {
                width: parent.width
                height: parent.height - 200
                clip: true
                background: Rectangle { color: "white"; radius: 10; border.color: "#dcdde1" }

                TextArea {
                    id: chatTextArea
                    text: _networkEngine.chatLog
                    font.pixelSize: 18
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    onTextChanged: { chatTextArea.cursorPosition = chatTextArea.text.length }
                }
            }

            Row {
                width: parent.width
                spacing: 10

                TextField {
                    id: messageInput
                    width: parent.width - 90
                    placeholderText: "Сообщение..."
                    font.pixelSize: 18
                    background: Rectangle { implicitHeight: 50; radius: 8; border.color: "#7f8fa6" }
                }

                Button {
                    width: 80
                    height: 50
                    text: "Послать"
                    contentItem: Text { text: parent.text; font.pixelSize: 16; font.bold: true; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: "#00a8ff"; radius: 8 }
                    onClicked: {
                        if (messageInput.text.trim() !== "") {
                            _networkEngine.sendTextMessage(messageInput.text.trim())
                            messageInput.text = ""
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: callScreen
        anchors.fill: parent
        color: "#1e272e"
        visible: _networkEngine.callStatus !== "IDLE"

        Column {
            anchors.centerIn: parent
            width: parent.width * 0.85
            spacing: 40

            Text {
                text: _networkEngine.activeChatPeer
                font.pixelSize: 42
                font.bold: true
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
            }

            Text {
                text: {
                    if (_networkEngine.callStatus === "OUTGOING") return "Вызываю абонента..."
                    if (_networkEngine.callStatus === "INCOMING") return "Входящий вызов..."
                    if (_networkEngine.callStatus === "CONNECTED") return "Разговор..."
                    return ""
                }
                font.pixelSize: 22
                color: "#dcdde1"
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 40

                Button {
                    id: acceptBtn
                    text: "📞"
                    visible: _networkEngine.callStatus === "INCOMING"
                    contentItem: Text { text: parent.text; font.pixelSize: 32; color: "white" }
                    background: Rectangle { implicitWidth: 85; implicitHeight: 80; radius: 40; color: "#4cd137" }
                    onClicked: { _networkEngine.acceptCall() }
                }

                Button {
                    id: rejectBtn
                    text: "❌"
                    contentItem: Text { text: parent.text; font.pixelSize: 32; color: "white" }
                    background: Rectangle { implicitWidth: 85; implicitHeight: 80; radius: 40; color: "#ff4757" }
                    onClicked: { _networkEngine.rejectOrEndCall() }
                }
            }
        }
    }

    Rectangle {
        id: vuMeterContainer
        width: parent.width - 40
        height: 25
        radius: 6
        color: "#dcdde1"
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 15
        anchors.horizontalCenter: parent.horizontalCenter
        visible: _networkEngine.callStatus === "CONNECTED"

        Rectangle {
            height: parent.height
            radius: 6
            color: "#4cd137"
            width: parent.width * window.currentVolume
            Behavior on width { NumberAnimation { duration: 50 } }
        }

        Text {
            text: "Микрофон"
            font.pixelSize: 12
            font.bold: true
            color: "#2f3640"
            anchors.centerIn: parent
        }
    }
}
