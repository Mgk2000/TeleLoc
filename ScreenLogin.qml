import QtQuick
import QtQuick.Controls

Rectangle {
    id: loginRoot
    anchors.fill: parent
    color: "#f5f6fa"

    signal registered() // Сигнал для переключения экрана наружу

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
                    _networkEngine.saveNameToFile(nameInput.text.trim());
                    loginRoot.registered(); // Стреляем сигналом наружу
                }
            }
        }
    }
}
