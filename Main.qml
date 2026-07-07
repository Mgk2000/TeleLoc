import QtQuick
import QtQuick.Controls

Window {
    id: window
    width: 360
    height: 640
    visible: true
    title: "TeleLoc - Телефон"
    color: "#f5f6fa"

    ListModel {
        id: contactsModel
        ListElement { name: "Иван" }
        ListElement { name: "Марья" }
        ListElement { name: "Петр" }
        ListElement { name: "Анфиса" }
    }

    // ЭКРАН 1: Ввод имени ОДИН РАЗ (Показывается, если файла конфигурации нет)
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: !_networkEngine.isRegistered // Сверяемся с C++ свойством файла

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
                background: Rectangle { implicitHeight: 60; radius: 10; border.color: "#7f8fa6" }
            }
            Button {
                width: parent.width; height: 60
                text: "Зарегистрировать гаджет"
                contentItem: Text { text: parent.text; font.pixelSize: 22; font.bold: true; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: "#00a8ff"; radius: 10 }
                onClicked: {
                    if (nameInput.text.trim() !== "") {
                        // Отправляем имя на намертво сохранение в файл C++
                        _networkEngine.saveNameToFile(nameInput.text.trim());
                    }
                }
            }
        }
    }

    // ЭКРАН 2: Главное окно со списком абонентов (Показывается, если файл успешно прочитан)
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: _networkEngine.isRegistered

        Column {
            anchors.fill: parent; anchors.margins: 20; spacing: 20

            Row {
                width: parent.width; spacing: 10
                Text { text: "Абонент: "; font.pixelSize: 18; color: "#718093" }
                Text { text: _networkEngine.myName; font.pixelSize: 18; font.bold: true; color: "#4cd137" }

                Button {
                    text: "Сброс имени"
                    anchors.right: parent.right
                    onClicked: {
                        _networkEngine.resetRegistration(); // Удалит файл конфигурации
                    }
                }
            }

            Text { text: "Кому звоним?"; font.pixelSize: 28; font.bold: true; color: "#2f3640" }

            ListView {
                width: parent.width; height: parent.height - 150
                model: contactsModel; spacing: 12
                delegate: Rectangle {
                    id: delegateRect
                    width: parent.width
                    visible: model.name !== _networkEngine.myName
                    height: model.name !== _networkEngine.myName ? 80 : 0
                    radius: 12; color: "white"; border.color: "#dcdde1"; border.width: 1
                    Row {
                        anchors.fill: parent; anchors.margins: 15; spacing: 20
                        Text { text: model.name; font.pixelSize: 24; font.bold: true; anchors.verticalCenter: parent.verticalCenter; color: "#2f3640" }
                        Button {
                            text: "📞"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            contentItem: Text { text: parent.text; font.pixelSize: 26; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { implicitWidth: 65; implicitHeight: 55; color: "#f5f6fa"; radius: 8 }
                            onClicked: {
                                _networkEngine.startCall(model.name);
                            }
                        }
                    }
                }
            }
        }
    }
}
