import QtQuick
import QtQuick.Controls

Window {
    id: window
    width: 360
    height: 640
    visible: true
    title: "TeleLoc"
    color: "#f5f6fa"

    property string activeScreen: "LOGIN"

    // Автоматический перехват входящего чата из C++
    Connections {
        target: _networkEngine
        function onRequestOpenChat(fromPeer) {
            window.activeScreen = "CHAT"
        }
    }

    // Автоматический пропуск регистрации при старте
    Component.onCompleted: {
        if (_networkEngine.isRegistered) {
            window.activeScreen = "MAIN"
        }
    }

    ListModel {
        id: contactsModel
        ListElement { name: "Иван" }
        ListElement { name: "Марья" }
        ListElement { name: "Петр" }
        ListElement { name: "Анфиса" }
    }

    // =========================================================================
    // ЭКРАН 1: Первичный ввод имени
    // =========================================================================
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: window.activeScreen === "LOGIN"

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
                placeholderText: "Имя абонента..."
                font.pixelSize: 22
                background: Rectangle {
                    implicitHeight: 60
                    radius: 10
                    border.color: "#7f8fa6"
                }
            }

            Button {
                width: parent.width
                height: 60
                text: "Зарегистрировать гаджет"

                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 22
                    font.bold: true
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: "#00a8ff"
                    radius: 10
                }

                onClicked: {
                    if (nameInput.text.trim() !== "") {
                        _networkEngine.saveNameToFile(nameInput.text.trim())
                        window.activeScreen = "MAIN"
                    }
                }
            }
        }
    }
    // =========================================================================
    // ЭКРАН 2: Главное окно со списком абонентов (Защищённая вёрстка Item)
    // =========================================================================
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: window.activeScreen === "MAIN"

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 20

            Button {
                text: "Сбросить регистрацию гаджета"
                anchors.right: parent.right
                onClicked: {
                    _networkEngine.resetRegistration()
                    window.activeScreen = "LOGIN"
                }
            }

            ListView {
                width: parent.width
                height: parent.height - 180
                model: contactsModel
                spacing: 12

                delegate: Rectangle {
                    id: delegateRect
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

                        Button {
                            text: "📝"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter

                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 26
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                implicitWidth: 65
                                implicitHeight: 55
                                color: "#eccc68"
                                radius: 8
                            }

                            onClicked: {
                                _networkEngine.activeChatPeer = model.name
                                window.activeScreen = "CHAT"
                            }
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    // ЭКРАН 3: Окно Чат-пейджера (Переписка)
    // =========================================================================
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: window.activeScreen === "CHAT"

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            Row {
                width: parent.width
                spacing: 15

                Button {
                    text: "⬅ Назад"
                    font.pixelSize: 16
                    onClicked: {
                        _networkEngine.activeChatPeer = ""
                        window.activeScreen = "MAIN"
                    }
                }

                Text {
                    text: "Чат: " + _networkEngine.activeChatPeer
                    font.pixelSize: 22
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            ScrollView {
                width: parent.width
                height: parent.height - 200
                clip: true
                background: Rectangle {
                    color: "white"
                    radius: 10
                    border.color: "#dcdde1"
                }

                TextArea {
                    text: _networkEngine.chatLog
                    font.pixelSize: 18
                    readOnly: true
                    wrapMode: TextArea.Wrap
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
                    background: Rectangle {
                        implicitHeight: 50
                        radius: 8
                        border.color: "#7f8fa6"
                    }
                }

                Button {
                    width: 80
                    height: 50
                    text: "Послать"

                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 16
                        font.bold: true
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: "#00a8ff"
                        radius: 8
                    }

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

    // =========================================================================
    // БЕЗОПАСНЫЙ ЦИФРОВОЙ ОТЛАДОЧНЫЙ ИНДИКАТОР (RMS замер звука)
    // =========================================================================
    Rectangle {
        id: vuMeterContainer
        width: parent.width - 40
        height: 35
        radius: 6
        color: "#2f3640"
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 15
        anchors.horizontalCenter: parent.horizontalCenter
        visible: window.activeScreen === "MAIN" || window.activeScreen === "CHAT"

        Text {
            // ИСПРАВЛЕНО: Прямое текстовое сложение строк, безопасное для компилятора!
            text: "Громкость RMS: " + _audioEngine.micVolume.toFixed(3)
            font.pixelSize: 14
            font.bold: true
            color: "#4cd137" // Зелёный цвет цифр
            anchors.centerIn: parent
        }
    }
}
