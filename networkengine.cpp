#include "networkengine.h"
#include <QDataStream>
#include <QNetworkDatagram>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent), m_port(45454)
{
    m_socket = new QUdpSocket(this);

    // Биндим порт с разрешением совместного использования в Wi-Fi сети
    m_socket->bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    connect(m_socket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);
}

void NetworkEngine::setMyName(const QString &name) {
    if (m_myName != name) {
        m_myName = name;
        emit myNameChanged();
    }
}

// Отправка текстового вызова в эфир
void NetworkEngine::startCall(const QString &targetName) {
    if (m_myName.isEmpty() || targetName.isEmpty()) return;

    QByteArray datagram;
    QDataStream out(&datagram, QIODevice::WriteOnly);

    // Пакет: ТИП_ПАКЕТА | КТО_ЗВОНИТ | КОМУ_ЗВОНИТ
    out << QString("CALL_REQ") << m_myName << targetName;

    m_socket->writeDatagram(datagram, QHostAddress::Broadcast, m_port);
    qDebug() << "===> [СЕТЬ] Наш пакет отправлен в эфир! От:" << m_myName << "Кому:" << targetName;
}

// Прием и фильтрация пакетов «Свой / Чужой»
void NetworkEngine::readPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_socket->pendingDatagramSize());
        m_socket->readDatagram(datagram.data(), datagram.size());

        QDataStream in(&datagram, QIODevice::ReadOnly);
        QString type, from, to;
        in >> type >> from >> to;

        // Если поймали собственное эхо — игнорируем
        if (from == m_myName) continue;

        qDebug() << "<=== [СЕТЬ] Поймали пакет в Wi-Fi! Тип:" << type << "От:" << from << "Кому:" << to;

        // ЖЕСТКАЯ ДАЧНАЯ ФИЛЬТРАЦИЯ
        if (to != m_myName) {
            qDebug() << "     [ФИЛЬТР] Пакет для" << to << ", а я" << m_myName << "-> Уничтожаю молча!";
            continue;
        }

        // Если пакет предназначен лично нам
        if (type == "CALL_REQ") {
            qDebug() << "     [УСПЕХ] Внимание! Нам звонит" << from << "!! Включаем логику вызова.";
        }
    }
}
