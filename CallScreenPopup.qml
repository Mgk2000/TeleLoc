// CallScreenPopup.qml
import QtQuick
import QtQuick.Controls

Popup {
    id: callScreenPopup
    width: parent ? parent.width : 360
    height: parent ? parent.height : 640

    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose

    // Позиционируем в левый верхний угол overlay-слоя
    x: 0
    y: 0

    parent: Overlay.overlay

    property string callerName: ""
    property int callNetType: -1
    property bool isIncoming: true

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
            color: "#1e4d34"
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
                                // Ваша логика принятия входящего
                                console.log("$$$ incoming accept 1");
                                window.activeConferencePeers = callScreenPopup.callerName;
                                window.activeCallNetType = callScreenPopup.callNetType;
                                console.log("$$$ incoming accept 2", callScreenPopup.callerName, callScreenPopup.callNetType );
                                netEngine.acceptAudioCall(callScreenPopup.callerName, callScreenPopup.callNetType);
                                console.log("$$$ incoming accept 3");
                            } else {
                                // Ваша логика для исходящего звонка, если двигаем вправо
                                // netEngine.startAudioCall(...) или аналогично

                                netEngine.startAudioCall(callScreenPopup.callerName, 0 | 0)
                            }
                            //callScreenPopup.close();
                        }
                        else if (handle.x < midX - threshold) {
                            handle.x = 2;

                            if (callScreenPopup.isIncoming) {
                                netEngine.stopAudioCall();
                            } else {
                                netEngine.stopAudioCall(); // Отмена исходящего
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
    }
}
