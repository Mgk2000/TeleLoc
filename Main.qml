import QtQuick
import QtQuick.Controls

Window {
    id: window
    width: 360
    height: 640
    visible: true
    title: "TeleLoc"
    color: "#f5f6fa"
    // ХИТРЫЙ ПЕРЕХВАТЧИК: Ловит сигнал из C++ и сам открывает экран переписки!
    Connections {
        target: _networkEngine
        function onRequestOpenChat(fromPeer) {
            window.activeScreen = "CHAT";
        }
    }

    // Локальное переключение экранов
    property string activeScreen: "LOGIN"

    // АВТОМАТИЧЕСКИЙ ПРОПУСК ПРИ СТАРТЕ (Если файл уже есть на диске)
    Component.onCompleted: {
        if (_networkEngine.isRegistered) {
            window.activeScreen = "MAIN";
        }
    }

    ListModel {
        id: contactsModel
        ListElement { name: "Иван" }
        ListElement { name: "Марья" }
        ListElement { name: "Петр" }
        ListElement { name: "Анфиса" }
    }

    // ЭКРАН 1: Первичный ввод имени
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
                font.pixelSize: 32; font.bold: true; horizontalAlignment: Text.AlignHCenter; width: parent.width; color: "#2f3640"
            }
            TextField {
                id: nameInput
                width: parent.width; placeholderText: "Имя абонента..."; font.pixelSize: 22
                background: Rectangle { implicitHeight: 60; radius: 10; border.color: "#7f8fa6" }
            }
            Button {
                width: parent.width; height: 60
                text: "Зарегистрировать гаджет"
                contentItem: Text { text: parent.text; font.pixelSize: 22; font.bold: true; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: "#00a8ff"; radius: 10 }
                onClicked: {
                    if (nameInput.text.trim() !== "") {
                        // 1. Отправляем на намертво сохранение в файл C++
                        _networkEngine.saveNameToFile(nameInput.text.trim());
                        // 2. ЖЕСТКО И ПРИНУДИТЕЛЬНО ПЕРЕКЛЮЧАЕМ ЭКРАН В ИНТЕРФЕЙСЕ ЛOКАЛЬНО!
                        window.activeScreen = "MAIN";
                    }
                }
            }
        }
    }

    // ЭКРАН 2: Главное окно со списком абонентов
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: window.activeScreen === "MAIN"

        Column {
            anchors.fill: parent; anchors.margins: 20; spacing: 20

            Button {
                text: "Сбросить регистрацию гаджета"
                anchors.right: parent.right
                onClicked: {
                    _networkEngine.resetRegistration(); // Удалит файлы конфигурации
                    window.activeScreen = "LOGIN";     // Локально вернет на первый экран
                }
            }

            ListView {
                width: parent.width; height: parent.height - 100
                model: contactsModel; spacing: 12
                delegate: Rectangle {
                    id: delegateRect
                    width: parent.width
                    visible: model.name !== _networkEngine.myName
                    height: model.name !== _networkEngine.myName ? 80 : 0
                    radius: 12; color: "white"; border.color: "#dcdde1"; border.width: 1

                    Row {
                        anchors.fill: parent; anchors.margins: 15; spacing: 15

                        Text { text: model.name; font.pixelSize: 24; font.bold: true; anchors.verticalCenter: parent.verticalCenter; color: "#2f3640" }

                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            Button {
                                text: "📝"
                                contentItem: Text { text: parent.text; font.pixelSize: 26; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { implicitWidth: 60; implicitHeight: 55; color: "#eccc68"; radius: 8 }
                                onClicked: {
                                    _networkEngine.activeChatPeer = model.name;
                                    window.activeScreen = "CHAT";
                                }
                            }

                            Button {
                                text: "📞"
                                contentItem: Text { text: parent.text; font.pixelSize: 26; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { implicitWidth: 60; implicitHeight: 55; color: "#4cd137"; radius: 8 }
                                onClicked: _networkEngine.startCall(model.name)
                            }
                        }
                    }
                }
            }
        }
    }

    // ЭКРАН 3: Окно Чат-пейджера (Переписка)
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: window.activeScreen === "CHAT"

        Column {
            anchors.fill: parent; anchors.margins: 20; spacing: 15

            Row {
                width: parent.width; spacing: 15
                Button {
                    text: "⬅ Назад"
                    font.pixelSize: 16
                    onClicked: {
                        _networkEngine.activeChatPeer = "";
                        window.activeScreen = "MAIN";
                    }
                }
                Text {
                    text: "Чат: " + _networkEngine.activeChatPeer
                    font.pixelSize: 22; font.bold: true; anchors.verticalCenter: parent.verticalCenter
                }
            }

            ScrollView {
                width: parent.width; height: parent.height - 160
                clip: true
                background: Rectangle { color: "white"; radius: 10; border.color: "#dcdde1" }

                TextArea {
                    text: _networkEngine.chatLog
                    font.pixelSize: 18; readOnly: true; wrapMode: TextArea.Wrap
                }
            }

            Row {
                width: parent.width; spacing: 10
                TextField {
                    id: messageInput
                    width: parent.width - 90; placeholderText: "Сообщение..."; font.pixelSize: 18
                    background: Rectangle { implicitHeight: 50; radius: 8; border.color: "#7f8fa6" }
                }
                Button {
                    width: 80; height: 50; text: "Послать"
                    contentItem: Text { text: parent.text; font.pixelSize: 16; font.bold: true; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: "#00a8ff"; radius: 8 }
                    onClicked: {
                        if (messageInput.text.trim() !== "") {
                            _networkEngine.sendTextMessage(messageInput.text.trim());
                            messageInput.text = "";
                        }
                    }
                }
            }
        }
    }
}
