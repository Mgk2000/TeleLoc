// CallScreenPopup.qml
import QtQuick
import QtQuick.Controls

Popup {
    id: callScreenPopup
    width: parent ? parent.width : 360
    height: parent ? (developer ? parent.height -250 : parent.height) : 640
    modal: ! developer
    focus: true
    closePolicy: Popup.NoAutoClose

    // Позиционируем в левый верхний угол overlay-слоя
    x: 0
    y:  developer ? 150 : 0

    parent: Overlay.overlay

    property string callerName: ""
    property int callNetType: -1
    property bool isIncoming: true

    // --- НАСТРОЙКИ УРОВНЯ ЗВУКА ДЛЯ ОТЛАДКИ ---
    property int micLevel: 50
    property int speakerLevel: 70

    background: Rectangle {
        color: "#0a2f1d" // Глубокий темно-зеленый
        anchors.fill: parent
    }

    contentItem: Item {
        anchors.fill: parent

        // --- ВЕРХНЯЯ ЧАСТЬ: Иконка телефона ---
        Rectangle {
            id: iconContainer
            width: 120
            height: 120
            radius: 60
            color: callingColor
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: parent.height * 0.15

            Text {
                text: "📞"
                font.pixelSize: 50
                anchors.centerIn: parent

                SequentialAnimation on scale {
                    running: callScreenPopup.isIncoming && callScreenPopup.visible
                    loops: Animation.Infinite
                    PropertyAnimation { to: 1.1; duration: 600; easing.type: Easing.InOutQuad }
                    PropertyAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
                }
            }
        }

        // --- РЕГУЛЯТОР МИКРОФОНА + ИНДИКАТОР ---
        Row {
            id: micController
            visible: developer
            spacing: 8
            anchors.right: iconContainer.left
            anchors.rightMargin: 15
            anchors.verticalCenter: iconContainer.verticalCenter

            // Сам VU-Meter (вертикальный индикатор)
            Rectangle {
                width: 6
                height: 60
                color: "#ff0000"
                radius: 3
                anchors.verticalCenter: parent.verticalCenter

                // Заполняющая зеленая полоска
                Rectangle {
                    width: parent.width
                    // Высота привязана к свойству из C++ уровня звука
                    height: parent.height * (audioEngine.currentMicLevel / 100.0)
                    color: audioEngine.currentMicLevel > 85 ? "#e74c3c" : "#2ecc71" // Краснеет при перегрузке
                    radius: parent.radius
                    anchors.bottom: parent.bottom

                    // Плавное падение индикатора вниз (чтобы не дергался слишком резко)
                    Behavior on height {
                        NumberAnimation { duration: 80; easing.type: Easing.OutQuad }
                    }
                }
            }

            Column {
                spacing: 5
                Text {
                    text: "🎙 Мик: " + callScreenPopup.micLevel
                    color: "#a0c0b0"
                    font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                SpinBox {
                    id: micSpinBox
                    width: 90
                    from: 0; to: 100; value: callScreenPopup.micLevel
                    editable: true; stepSize: 5
                    onValueModified: {
                        callScreenPopup.micLevel = value
                        audioEngine.setMicLevel(value)
                    }
                }
            }
        }

        // --- РЕГУЛЯТОР ДИНАМИКОВ + ИНДИКАТОР ---
        Row {
            id: speakerController
            visible: developer
            spacing: 8
            anchors.left: iconContainer.right
            anchors.leftMargin: 15
            anchors.verticalCenter: iconContainer.verticalCenter

            Column {
                spacing: 5
                Text {
                    text: "🔊 Звк: " + callScreenPopup.speakerLevel
                    color: "#a0c0b0"
                    font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                SpinBox {
                    id: speakerSpinBox
                    width: 90
                    from: 0; to: 100; value: callScreenPopup.speakerLevel
                    editable: true; stepSize: 5
                    onValueModified: {
                        callScreenPopup.speakerLevel = value
                        audioEngine.setSpeakerLevel(value)
                    }
                }
            }

            // VU-Meter для динамиков
            Rectangle {
                width: 6
                height: 60
                color: "#143d26"
                radius: 3
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: parent.width
                    height: parent.height * (audioEngine.currentSpeakerLevel / 100.0)
                    color: audioEngine.currentSpeakerLevel > 85 ? "#e74c3c" : "#3498db" // Синеет/краснеет
                    radius: parent.radius
                    anchors.bottom: parent.bottom

                    Behavior on height {
                        NumberAnimation { duration: 80; easing.type: Easing.OutQuad }
                    }
                }
            }
        }
        // --- СРЕДНЯЯ ЧАСТЬ: Имя абонента и статус ---
        Column {
            id: infoColumn
            anchors.top: iconContainer.bottom
            anchors.topMargin: 40
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            width: parent.width * 0.8

            Text {
                text: callScreenPopup.callerName
                color: "white"
                font.pixelSize: 28
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
                elide: Text.ElideRight
            }

            Text {
                text: callScreenPopup.isIncoming ? "Входящий вызов..." : "Исходящий вызов..."
                color: "#8bc34a"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }

        // --- НИЖНЯЯ ЧАСТЬ: 3-state Слайдер ---
        Item {
            id: sliderContainer
            width: parent.width * 0.85
            height: 70
            anchors.bottom: parent.bottom
            anchors.bottomMargin: parent.height * 0.08
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle {
                anchors.fill: parent
                color: "#143d26"
                radius: height / 2
                border.color: "#275d3d"
                border.width: 1

                Text {
                    text: callScreenPopup.isIncoming ? "◀ Отклонить" : "◀ Отмена"
                    color: "#a0c0b0"
                    font.pixelSize: 14
                    anchors.left: parent.left
                    anchors.leftMargin: 25
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: callScreenPopup.isIncoming ? "Принять ▶" : "Вызов ▶"
                    color: "#a0c0b0"
                    font.pixelSize: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 25
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Rectangle {
                id: handle
                width: 66
                height: 66
                radius: 33
                color: "white"
                anchors.verticalCenter: parent.verticalCenter

                x: (sliderContainer.width - width) / 2

                Text {
                    text: "↔"
                    font.pixelSize: 24
                    font.bold: true
                    color: handle.x < ((sliderContainer.width - handle.width) / 2 - 20) ? "#e74c3c" :
                           handle.x > ((sliderContainer.width - handle.width) / 2 + 20) ? "#2ecc71" : "#143d26"
                    anchors.centerIn: parent
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    drag.target: handle
                    drag.axis: Drag.XAxis
                    drag.minimumX: 2
                    drag.maximumX: sliderContainer.width - handle.width - 2

                    onReleased: {
                        var midX = (sliderContainer.width - handle.width) / 2;
                        var threshold = sliderContainer.width * 0.25;

                        if (handle.x > midX + threshold) {
                            handle.x = sliderContainer.width - handle.width - 2;

                            if (callScreenPopup.isIncoming) {
                                console.log("$$$ incoming accept 1");
                                window.activeConferencePeers = callScreenPopup.callerName;
                                window.activeCallNetType = callScreenPopup.callNetType;
                                console.log("$$$ incoming accept 2", callScreenPopup.callerName, callScreenPopup.callNetType );
                                netEngine.acceptAudioCall(callScreenPopup.callerName, callScreenPopup.callNetType);
                                console.log("$$$ incoming accept 3");
                            } else {
                                netEngine.startAudioCall(callScreenPopup.callerName, 0 | 0)
                            }
                        }
                        else if (handle.x < midX - threshold) {
                            handle.x = 2;

                            if (callScreenPopup.isIncoming) {
                                netEngine.stopAudioCall();
                            } else {
                               netEngine.stopAudioCall();
                            }
                            callScreenPopup.close();
                        }
                        else {
                            returnToCenterAnim.start();
                        }
                    }
                }

                NumberAnimation on x {
                    id: returnToCenterAnim
                    to: (sliderContainer.width - handle.width) / 2
                    duration: 200
                    easing.type: Easing.OutQuad
                    running: false
                }
            }
        }
    }

    onOpened: {
        handle.x = (sliderContainer.width - handle.width) / 2;

        // Синхронизируем интерфейс с текущими значениями из C++ при открытии окна
        micSpinBox.value = callScreenPopup.micLevel
        speakerSpinBox.value = callScreenPopup.speakerLevel
    }
}
