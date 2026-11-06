import QtQuick
import QtQuick.Controls

SpinBox {
    id: control
    editable: false
    // Значения по умолчанию, которые можно переопределить
    width: 90
    height: 40
    contentItem: Item {
           // Обнуляем ширину и высоту текстового поля
           implicitWidth: 0
           implicitHeight: 0
           width: 0
           height: 0
           visible: false // Скрываем, так как текст нам больше не нужен
       }
    // Используем наш шаблон для кнопки Минус
    down.indicator: SpinButton {
        x: 0
        y: 0
        symbol: "−"
        isPressed: control.down.pressed
    }

    // Используем наш шаблон для кнопки Плюс
    up.indicator: SpinButton {
        x: control.width - width
        y: 0
        symbol: "+"
        isPressed: control.up.pressed
    }
}
