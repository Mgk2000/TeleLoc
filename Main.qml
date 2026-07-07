import QtQuick
import QtQuick.Controls

Window {
    id: window
    width: 360
    height: 640
    visible: true
    title: "TeleLoc - Телефон"
    color: "#f5f6fa"

    property string myName: ""
    property string activeScreen: "LOGIN"

    // Экран ввода никнейма
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: window.activeScreen === "LOGIN"
        Column {
            anchors.centerIn: parent
            width: parent.width * 0.85
            spacing: 25
            Text {
                text: "Регистрация\nв TeleLoc"
                font.pixelSize: 32
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
            }
            TextField {
                id: nameInput
                width: parent.width
                placeholderText: "Ваше имя..."
                font.pixelSize: 22
                background: Rectangle { implicitHeight: 60; radius: 10; border.color: "#7f8fa6" }
            }
            Button {
                width: parent.width; height: 60
                text: "Войти"
                contentItem: Text { text: parent.text; font.pixelSize: 22; font.bold: true; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: "#00a8ff"; radius: 10 }
                onClicked: if (nameInput.text.trim() !== "") { window.myName = nameInput.text.trim(); window.activeScreen = "MAIN"; }
            }
        }
    }

    // Экран списка контактов
    Rectangle {
        anchors.fill: parent
        color: parent.color
        visible: window.activeScreen === "MAIN"
        Column {
            anchors.fill: parent; anchors.margins: 20; spacing: 20
            Text { text: "Кому звоним?"; font.pixelSize: 28; font.bold: true }

            ListModel {
                id: contactsModel
                ListElement { name: "Иван" }
                ListElement { name: "Марья" }
                ListElement { name: "Петр" }
                ListElement { name: "Анфиса" }
            }

            ListView {
                width: parent.width; height: parent.height - 100
                model: contactsModel; spacing: 12
                delegate: Rectangle {
                    id: delegateRect
                    width: parent.width; height: 80
                    visible: model.name !== window.myName
                    radius: 12; color: "white"
                    Row {
                        anchors.fill: parent; anchors.margins: 15; spacing: 20
                        Text { text: model.name; font.pixelSize: 22; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
                        Button {
                            text: "📞"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            contentItem: Text { text: parent.text; font.pixelSize: 26; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { implicitWidth: 60; implicitHeight: 50; color: "#f5f6fa"; radius: 8 }
                            onClicked: console.log("Вызов: " + model.name)
                        }
                    }
                }
            }
        }
    }
}
