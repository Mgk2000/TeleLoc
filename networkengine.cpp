#include "networkengine.h"
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent), tcpClientSocket(nullptr), m_activeCallNetType(-1) {

    UserInfo me;
    me.name = "Abonent";
    me.ip0 = ""; me.ip1 = ""; me.ip2 = "";
    me.lastSeen = QDateTime::currentDateTime();
    me.isAlive = true;
    m_users.append(me);

    udpSocket = new QUdpSocket(this);
    udpSocket->bind(PORT, QUdpSocket::ShareAddress);
    connect(udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);

    tcpServer = new QTcpServer(this);
    tcpServer->listen(QHostAddress::Any, PORT);
    connect(tcpServer, &QTcpServer::newConnection, this, &NetworkEngine::onNewConnection);

    tcpSocket = new QTcpSocket(this);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &NetworkEngine::onReadyTcpRead);

    audioEngine = new AudioEngine(this);

    readConfig();
    updateInterfaces();

    interfaceTimer = new QTimer(this);
    connect(interfaceTimer, &QTimer::timeout, this, &NetworkEngine::updateInterfaces);
    interfaceTimer->start(10000);

    discoveryTimer = new QTimer(this);
    connect(discoveryTimer, &QTimer::timeout, this, &NetworkEngine::sendDiscovery);
    discoveryTimer->start(10000);
}

NetworkEngine::~NetworkEngine() {}

bool NetworkEngine::isNetTypeAvailable(int netType) const {
    if (m_users.isEmpty()) return false;
    UserInfo me = m_users.at(0);
    if (netType == 0) return !me.ip0.isEmpty();
    if (netType == 1) return !me.ip1.isEmpty();
    if (netType == 2) return !me.ip2.isEmpty();
    return false;
}

QVariantList NetworkEngine::getUsers(int netType) const {
    QVariantList list;
    if (m_users.isEmpty() || !isNetTypeAvailable(netType)) return list;

    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].isAlive) {
            QString targetIp = "";
            if (netType == 0 && !m_users[i].ip0.isEmpty()) targetIp = m_users[i].ip0;
            else if (netType == 1 && !m_users[i].ip1.isEmpty()) targetIp = m_users[i].ip1;
            else if (netType == 2 && !m_users[i].ip2.isEmpty()) targetIp = m_users[i].ip2;

            if (!targetIp.isEmpty()) {
                QVariantMap map;
                map["name"] = m_users[i].name;
                map["ip"] = targetIp;
                list.append(map);
            }
        }
    }
    return list;
}

void NetworkEngine::parseIncomingSyncData(const QByteArray &data, const QString &senderIpStr) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return;
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    QString name = obj["name"].toString();

    if (name == m_users.at(0).name) return;

    if (type == "discovery" || type == "users_sync" || type == "incoming_call" || type == "accept_call" || type == "stop_call") {
        QString ip0 = obj["ip0"].toString();
        QString ip1 = obj["ip1"].toString();
        QString ip2 = obj["ip2"].toString();

        if (ip0.isEmpty() && senderIpStr.startsWith("192.168.") && !senderIpStr.startsWith("192.168.43.") && !senderIpStr.startsWith("192.168.49.")) {
            ip0 = senderIpStr;
        } else if (ip1.isEmpty() && senderIpStr.startsWith("192.168.43.")) {
            ip1 = senderIpStr;
        } else if (ip2.isEmpty() && senderIpStr.startsWith("192.168.49.")) {
            ip2 = senderIpStr;
        }

        bool found = false;
        for (int i = 1; i < m_users.size(); ++i) {
            if ((!ip2.isEmpty() && m_users[i].ip2 == ip2) || (!ip1.isEmpty() && m_users[i].ip1 == ip1) || (!ip0.isEmpty() && m_users[i].ip0 == ip0) || m_users[i].name == name) {
                m_users[i].name = name;
                if (!ip0.isEmpty()) m_users[i].ip0 = ip0;
                if (!ip1.isEmpty()) m_users[i].ip1 = ip1;
                if (!ip2.isEmpty()) m_users[i].ip2 = ip2;
                m_users[i].lastSeen = QDateTime::currentDateTime();
                m_users[i].isAlive = true;
                found = true;
                break;
            }
        }

        if (!found && !name.isEmpty()) {
            UserInfo u;
            u.name = name; u.ip0 = ip0; u.ip1 = ip1; u.ip2 = ip2;
            u.lastSeen = QDateTime::currentDateTime();
            u.isAlive = true;
            m_users.append(u);
        }
        emit peerListChanged();
    }

    if (type == "message") {
        emit messageReceived(name, obj["text"].toString());
    } else if (type == "incoming_call") {
        int netType = obj["net_type"].toInt();
        emit incomingCall(name, netType);
    } else if (type == "accept_call") {
        emit callAccepted();
    } else if (type == "stop_call") {
        emit callStopped();
    }
}

void NetworkEngine::onNewConnection() {
    tcpClientSocket = tcpServer->nextPendingConnection();
    connect(tcpClientSocket, &QTcpSocket::readyRead, this, &NetworkEngine::onReadyTcpRead);

    UserInfo me = m_users.at(0);
    QJsonObject syncObj;
    syncObj["type"] = "users_sync";
    syncObj["name"] = me.name;
    syncObj["ip0"] = me.ip0; syncObj["ip1"] = me.ip1; syncObj["ip2"] = me.ip2;
    tcpClientSocket->write(QJsonDocument(syncObj).toJson(QJsonDocument::Compact));
}

void NetworkEngine::onReadyTcpRead() {
    QTcpSocket *senderSocket = qobject_cast<QTcpSocket*>(sender());
    if (!senderSocket) return;
    parseIncomingSyncData(senderSocket->readAll(), senderSocket->peerAddress().toString());
}

void NetworkEngine::sendDiscovery() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);

    QJsonObject obj;
    obj["type"] = "discovery";
    obj["name"] = me.name;
    obj["ip0"] = me.ip0; obj["ip1"] = me.ip1; obj["ip2"] = me.ip2;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    if (!me.ip0.isEmpty()) udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), PORT);
    if (!me.ip1.isEmpty()) {
        udpSocket->writeDatagram(data, QHostAddress("192.168.43.255"), PORT);
        udpSocket->writeDatagram(data, QHostAddress("192.168.137.255"), PORT);
    }

    if (!me.ip2.isEmpty() && me.ip2 != SERVER_IP) {
        if (tcpSocket && tcpSocket->state() == QAbstractSocket::UnconnectedState) {
            tcpSocket->connectToHost(SERVER_IP, PORT);
        }
        if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
            tcpSocket->write(data);
        }
    }
}

void NetworkEngine::readPendingDatagrams() {
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress senderIp;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &senderIp);
        quint32 ipv4Int = senderIp.toIPv4Address();
        QString ipStr = (ipv4Int != 0) ? QHostAddress(ipv4Int).toString() : senderIp.toString();
        if (ipStr == "127.0.0.1" || ipStr == "::1") continue;
        parseIncomingSyncData(datagram, ipStr);
    }
}

void NetworkEngine::sendMessage(const QString &targetPeer, const QString &text) {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);

    QJsonObject obj;
    obj["type"] = "message";
    obj["name"] = me.name;
    obj["text"] = text;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    // РЕЖИМ 1: Вещание на "Все"
    if (targetPeer == "Все") {
        bool udpSent = false;

        // Если у нас активны LAN или AP интерфейсы, бьем широким UDP-вещанием
        if (!me.ip0.isEmpty()) {
            udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), PORT);
            udpSent = true;
        }
        if (!me.ip1.isEmpty()) {
            udpSocket->writeDatagram(data, QHostAddress("192.168.43.255"), PORT);
            udpSocket->writeDatagram(data, QHostAddress("192.168.137.255"), PORT);
            udpSent = true;
        }

        // Если UDP пустить некуда (мы сидим строго в Wi-Fi Direct), делаем веерную рассылку по TCP
        if (!udpSent || !me.ip2.isEmpty()) {
            for (int i = 1; i < m_users.size(); ++i) {
                if (m_users[i].isAlive) {
                    QString directIp = m_users[i].ip2;
                    if (!directIp.isEmpty()) {
                        QTcpSocket tmpSocket;
                        tmpSocket.connectToHost(directIp, PORT);
                        if (tmpSocket.waitForConnected(500)) {
                            tmpSocket.write(data);
                            tmpSocket.waitForBytesWritten(500);
                        }
                    }
                }
            }
        }
        return;
    }

    // РЕЖИМ 2: Личное сообщение конкретному дачнику (Иван <-> Пётр)
    QString targetIp = "";

    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].name == targetPeer && m_users[i].isAlive) {
            // Строгий приоритет выбора сети: LAN (ip0) -> AP (ip1) -> Direct (ip2)
            if (!m_users[i].ip0.isEmpty() && !me.ip0.isEmpty()) targetIp = m_users[i].ip0;
            else if (!m_users[i].ip1.isEmpty() && !me.ip1.isEmpty()) targetIp = m_users[i].ip1;
            else if (!m_users[i].ip2.isEmpty() && !me.ip2.isEmpty()) targetIp = m_users[i].ip2;
            break;
        }
    }

    // Отправляем пакет по выбранному IP адресу
    if (!targetIp.isEmpty()) {
        // Если это Wi-Fi Direct, проверяем фоновые сокеты для ускорения
        if (targetIp.startsWith("192.168.49.")) {
            if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
                tcpClientSocket->write(data);
                return;
            }
            if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
                tcpSocket->write(data);
                return;
            }
        }

        // Универсальный быстрый TCP-выстрел для LAN/AP/Direct
        QTcpSocket tmpSocket;
        tmpSocket.connectToHost(targetIp, PORT);
        if (tmpSocket.waitForConnected(1000)) {
            tmpSocket.write(data);
            tmpSocket.waitForBytesWritten(500);
        }
    }
}

void NetworkEngine::startAudioCall(const QString &targetPeerName, int netType) {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QString targetIp = "";

    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].name == targetPeerName && m_users[i].isAlive) {
            if (netType == 0) targetIp = m_users[i].ip0;
            else if (netType == 1) targetIp = m_users[i].ip1;
            else if (netType == 2) targetIp = m_users[i].ip2;
            break;
        }
    }

    if (!targetIp.isEmpty()) {
        m_activePeerIp = targetIp;
        m_activeCallNetType = netType;

        QJsonObject obj;
        obj["type"] = "incoming_call"; obj["name"] = me.name; obj["net_type"] = netType;
        obj["ip0"] = me.ip0; obj["ip1"] = me.ip1; obj["ip2"] = me.ip2;
        QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

        // Универсальный TCP-выстрел для ВСЕХ типов сетей:
        tcpSocket->abort();
        tcpSocket->connectToHost(targetIp, PORT);
        if (tcpSocket->waitForConnected(1500)) {
            tcpSocket->write(data);
            tcpSocket->waitForBytesWritten(1000);
        } else {
            qDebug() << "=== TCP CALL ERROR ===" << "Cannot connect to host:" << targetIp;
        }
    }
}

void NetworkEngine::acceptAudioCall(const QString &targetPeerName, int netType) {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    m_activeCallNetType = netType;

    QJsonObject obj;
    obj["type"] = "accept_call"; obj["name"] = me.name;
    obj["ip0"] = me.ip0; obj["ip1"] = me.ip1; obj["ip2"] = me.ip2;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    // Если к нам подключились, шлём ответ в активный канал
    if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
        tcpClientSocket->write(data);
        tcpClientSocket->waitForBytesWritten(500);
    } else {
        tcpSocket->write(data);
        tcpSocket->waitForBytesWritten(500);
    }

    // Физический старт вашего аудио-движка (передаём IP и ПОРТ из Gist)
   // audioEngine->start(m_activePeerIp, PORT);
}

void NetworkEngine::stopAudioCall() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QJsonObject obj;
    obj["type"] = "stop_call"; obj["name"] = me.name;
    obj["ip0"] = me.ip0; obj["ip1"] = me.ip1; obj["ip2"] = me.ip2;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
        tcpClientSocket->write(data);
        tcpClientSocket->waitForBytesWritten(500);
        tcpClientSocket->disconnectFromHost();
    }
    if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
        tcpSocket->write(data);
        tcpSocket->waitForBytesWritten(500);
        tcpSocket->disconnectFromHost();
    }

    // Физическая остановка вашего аудио-движка из Gist
    audioEngine->stop();

    m_activePeerIp = "";
    m_activeCallNetType = -1;
    emit callStopped();
}
void NetworkEngine::updateInterfaces() {
    if (m_users.isEmpty()) return;
    m_users[0].ip0 = ""; m_users[0].ip1 = ""; m_users[0].ip2 = "";
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if ((interface.flags() & QNetworkInterface::IsUp) && !(interface.flags() & QNetworkInterface::IsLoopBack)) {
            for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    QString ipStr = entry.ip().toString();
                    if (ipStr.startsWith("192.168.")) {
                        if (ipStr.startsWith("192.168.43.")) m_users[0].ip1 = ipStr;
                        else if (ipStr.startsWith("192.168.49.")) m_users[0].ip2 = ipStr;
                        else m_users[0].ip0 = ipStr;
                    }
                }
            }
        }
    }
}

void NetworkEngine::readConfig() {
    if (m_users.isEmpty()) return;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_users[0].name = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
    }
}

void NetworkEngine::saveNameToFile(const QString &name) {
    if (m_users.isEmpty()) return;
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf");
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file); out << name.trimmed(); file.close();
        m_users[0].name = name.trimmed();
    }
}

void NetworkEngine::debugUsers() const {
    qDebug() << "=== TELELOC DEBUG USERS ===";
    for (int i = 0; i < m_users.size(); ++i) {
        qDebug() << "Index:" << i
                 << "Name:" << m_users[i].name.toLocal8Bit().constData()
                 << "LAN:" << m_users[i].ip0
                 << "AP:" << m_users[i].ip1
                 << "P2P:" << m_users[i].ip2
                 << "Alive:" << m_users[i].isAlive;
    }
}

QString NetworkEngine::getSavedName() const {
    return m_users.isEmpty() ? "" : m_users.at(0).name;
}
