import QtQuick
import QtQuick.Controls

Rectangle {
    id: callRoot
    anchors.fill: parent
    color: "#1e272e" // Стильный темно-серый фон экрана звонка

    Column {
        anchors.centerIn: parent
        width: parent.width * 0.85
        spacing: 40

        // Имя абонента на проводе
        Text {
            text: _networkEngine.activeChatPeer
            font.pixelSize: 40
            font.bold: true
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        // Текстовый статус текущего состояния сокета C++
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

        // НИЖНЯЯ ПАНЕЛЬ ИНТЕРАКТИВНЫХ КНОПОК
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 30

            // Зелёная кнопка «ОТВЕТИТЬ» (Показывается только при входящем звонке)
            Button {
                text: "📞"
                visible: _networkEngine.callStatus === "INCOMING"
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 32
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 80
                    implicitHeight: 80
                    radius: 40
                    color: "#4cd137"
                }
                onClicked: {
                    _networkEngine.acceptCall()
                }
            }

            // Красная кнопка «СБРОСИТЬ / ОТКЛОНИТЬ» (Есть всегда при любом статусе звонка)
            Button {
                text: "❌"
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 32
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 80
                    implicitHeight: 80
                    radius: 40
                    color: "#ff4757"
                }
                onClicked: {
                    _networkEngine.rejectOrEndCall()
                }
            }
        }
    }
}
