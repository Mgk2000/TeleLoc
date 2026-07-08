#include "networkengine.h"
#include <QDataStream>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent), m_port(45454), m_isRegistered(false), m_activeChatPeer("")
{
    m_socket = new QUdpSocket(this);
    m_socket->bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    connect(m_socket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);

    loadNameFromFile();
    loadQueueFromFile();

    m_pingTimer = new QTimer(this);
    connect(m_pingTimer, &QTimer::timeout, this, &NetworkEngine::onPingTimer);
    m_pingTimer->start(10000);
}

QString NetworkEngine::getConfigPath(const QString &fileName) const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/" + fileName;
}

void NetworkEngine::loadNameFromFile() {
    QFile file(getConfigPath("teleloc.conf"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString savedName = in.readAll().trimmed();
        file.close();
        if (!savedName.isEmpty()) {
            m_myName = savedName;
            m_isRegistered = true;
            emit myNameChanged();
            emit isRegisteredChanged();
        }
    }
}

void NetworkEngine::saveNameToFile(const QString &name) {
    QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) return;
    QFile file(getConfigPath("teleloc.conf"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << trimmedName;
        file.close();

        m_myName = trimmedName;
        m_isRegistered = true;

        emit myNameChanged();
        emit isRegisteredChanged();
        qDebug() << "===> [КОНФИГ] УСПЕХ: Имя" << m_myName << "сохранено в файл!";
    }
}

void NetworkEngine::setMyName(const QString &name) {
    if (m_myName != name) {
        m_myName = name;
        emit myNameChanged();
    }
}

void NetworkEngine::resetRegistration() {
    QFile file(getConfigPath("teleloc.conf"));
    file.remove();
    QFile qFile(getConfigPath("pending_messages.conf"));
    qFile.remove();

    // Вычищаем сохраненную историю чатов при полном сбросе
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QStringList filters; filters << "chat_*.log";
    for (const QString &f : dir.entryList(filters)) { dir.remove(f); }

    m_offlineQueue.clear();
    m_myName = "";
    m_isRegistered = false;
    m_chatLog = "";
    m_activeChatPeer = "";
    emit myNameChanged();
    emit isRegisteredChanged();
    emit chatLogChanged();
    emit activeChatPeerChanged();
}

// ВХОД В ЧАТ: Считываем всю историю из файла на диске!
void NetworkEngine::setActiveChatPeer(const QString &peer) {
    if (m_activeChatPeer != peer) {
        m_activeChatPeer = peer;
        emit activeChatPeerChanged();

        m_chatLog = "";
        // Пытаемся открыть файл истории сообщений с этим конкретным человеком
        QFile file(getConfigPath("chat_" + peer + ".log"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            m_chatLog = in.readAll();
            file.close();
        }

        if (m_chatLog.isEmpty()) {
            m_chatLog = QString("--- Начало чата с %1 ---\n").arg(peer);
        }
        emit chatLogChanged();
    }
}

// ОТПРАВКА КНОПКОЙ ИЗ QML
void NetworkEngine::sendTextMessage(const QString &text) {
    if (m_myName.isEmpty() || m_activeChatPeer.isEmpty() || text.trimmed().isEmpty()) return;

    QString msgText = text.trimmed();
    QString formattedLine = QString("[Вы]: %1\n").arg(msgText);

    // 1. Обновляем экран переписки
    m_chatLog += formattedLine;
    emit chatLogChanged();

    // 2. ЗАПИСЫВАЕМ СВОЮ СТРОКУ В ФАЙЛ ИСТОРИИ НА ДИСК!
    QFile file(getConfigPath("chat_" + m_activeChatPeer + ".log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file); out << formattedLine; file.close();
    }

    QByteArray datagram;
    QDataStream out(&datagram, QIODevice::WriteOnly);
    out << QString("TEXT_MSG") << m_myName << m_activeChatPeer << msgText;
    m_socket->writeDatagram(datagram, QHostAddress::Broadcast, m_port);

    OfflineMessage newMsg = { m_myName, m_activeChatPeer, msgText, QDateTime::currentDateTime() };
    m_offlineQueue.append(newMsg);
    cleanOldMessages();
    saveQueueToFile();
}

void NetworkEngine::onPingTimer() {
    if (m_myName.isEmpty() || m_offlineQueue.isEmpty()) return;
    cleanOldMessages();
    for (const auto &msg : m_offlineQueue) {
        QByteArray datagram;
        QDataStream out(&datagram, QIODevice::WriteOnly);
        out << QString("PING_REQ") << m_myName << msg.to;
        m_socket->writeDatagram(datagram, QHostAddress::Broadcast, m_port);
    }
}

void NetworkEngine::cleanOldMessages() {
    QDateTime now = QDateTime::currentDateTime();
    auto it = m_offlineQueue.begin();
    while (it != m_offlineQueue.end()) {
        if (it->timestamp.secsTo(now) > 86400) {
            it = m_offlineQueue.erase(it);
        } else {
            ++it;
        }
    }
    if (m_offlineQueue.size() > 5) {
        while (m_offlineQueue.size() > 5) {
            m_offlineQueue.removeFirst();
        }
    }
}

void NetworkEngine::saveQueueToFile() {
    QFile file(getConfigPath("pending_messages.conf"));
    if (file.open(QIODevice::WriteOnly)) {
        QDataStream out(&file);
        out << m_offlineQueue.size();
        for (const auto &msg : m_offlineQueue) {
            out << msg.from << msg.to << msg.text << msg.timestamp;
        }
        file.close();
    }
}

void NetworkEngine::loadQueueFromFile() {
    QFile file(getConfigPath("pending_messages.conf"));
    if (file.open(QIODevice::ReadOnly)) {
        QDataStream in(&file);
        int size = 0;
        in >> size;
        m_offlineQueue.clear();
        for (int i = 0; i < size; ++i) {
            OfflineMessage msg;
            in >> msg.from >> msg.to >> msg.text >> msg.timestamp;
            m_offlineQueue.append(msg);
        }
        file.close();
        cleanOldMessages();
    }
}

void NetworkEngine::startCall(const QString &targetName) {
    if (m_myName.isEmpty() || targetName.isEmpty()) return;
    QByteArray datagram;
    QDataStream out(&datagram, QIODevice::WriteOnly);
    out << QString("CALL_REQ") << m_myName << targetName;
    m_socket->writeDatagram(datagram, QHostAddress::Broadcast, m_port);
}

// ПРИЕМ ПАКЕТОВ ИЗ WI-FI (РАБОТАЕТ ВСЕГДА В ФОНЕ)
void NetworkEngine::readPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_socket->pendingDatagramSize());
        m_socket->readDatagram(datagram.data(), datagram.size());

        QDataStream in(&datagram, QIODevice::ReadOnly);
        QString type, from, to;
        in >> type >> from >> to;

        if (from == m_myName) continue;
        if (to != m_myName) continue;

        if (type == "TEXT_MSG") {
            QString text;
            in >> text;

            QString formattedLine = QString("[%1]: %2\n").arg(from, text);

            // 1. Пишем в файл истории на диске
            QFile file(getConfigPath("chat_" + from + ".log"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&file); out << formattedLine; file.close();
            }

            // 2. Если чат с этим человеком закрыт — силой открываем его!
            if (m_activeChatPeer != from) {
                // Вызываем метод setActiveChatPeer, который сам подгрузит историю из файла
                setActiveChatPeer(from);

                // ВЫСТРЕЛИВАЕМ СИГНАЛ В QML ДЛЯ АВТО-ПЕРЕКЛЮЧЕНИЯ ЭКРАНА!
                emit requestOpenChat(from);
            } else {
                // Если чат уже был открыт — просто дописываем строчку на экран
                m_chatLog += formattedLine;
                emit chatLogChanged();
            }

            // Отправляем подтверждение доставки (TEXT_ACK)
            QByteArray datagramAck;
            QDataStream outAck(&datagramAck, QIODevice::WriteOnly);
            outAck << QString("TEXT_ACK") << m_myName << from << text;
            m_socket->writeDatagram(datagramAck, QHostAddress::Broadcast, m_port);
        }
        else if (type == "TEXT_ACK") {
            QString text;
            in >> text;
            auto it = m_offlineQueue.begin();
            while (it != m_offlineQueue.end()) {
                if (it->to == from && it->text == text) {
                    it = m_offlineQueue.erase(it);
                    qDebug() << "===> [ПЕЙДЖЕР] Сообщение доставлено абоненту" << from;
                } else {
                    ++it;
                }
            }
            saveQueueToFile();
        }
        else if (type == "PING_REQ") {
            QByteArray datagramPong;
            QDataStream outPong(&datagramPong, QIODevice::WriteOnly);
            outPong << QString("PING_PONG") << m_myName << from;
            m_socket->writeDatagram(datagramPong, QHostAddress::Broadcast, m_port);
        }
        else if (type == "PING_PONG") {
            qDebug() << "<=== [ПЕЙДЖЕР] Абонент" << from << "в сети. Отправляем очередь.";
            for (const auto &msg : m_offlineQueue) {
                if (msg.to == from) {
                    QByteArray datagramMsg;
                    QDataStream outMsg(&datagramMsg, QIODevice::WriteOnly);
                    outMsg << QString("TEXT_MSG") << m_myName << from << msg.text;
                    m_socket->writeDatagram(datagramMsg, QHostAddress::Broadcast, m_port);
                }
            }
        }
    }
}
