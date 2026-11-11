import QtQuick

Rectangle {
    id: root

    // Свойства, которые можно будет настроить при создании
    property string symbol: "+"
    property bool isPressed: false

    width: parent.width
    height: width

    // Цвета по умолчанию (темно-зеленый)
    color: isPressed ? "#0A3622" : "#0F5132"

    Text {
        text: root.symbol
        color: "#FFFFFF" // Всегда белый
        font.pixelSize: parent.height * 0.5
        font.bold: true
        anchors.centerIn: parent
    }
}
