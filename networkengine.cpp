#include "networkengine.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QTimer>

NetworkEngine::NetworkEngine(QObject *parent) : QObject(parent), tcpServer(nullptr), tcpSocket(nullptr), tcpClientSocket(nullptr) {
    udpSocket = new QUdpSocket(this);
    udpSocket->bind(PORT, QUdpSocket::ShareAddress);
    connect(udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::readPendingDatagrams);

    tcpServer = new QTcpServer(this);
    tcpServer->listen(QHostAddress::Any, PORT);
    connect(tcpServer, &QTcpServer::newConnection, this, &NetworkEngine::onNewConnection);

    tcpSocket = new QTcpSocket(this);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &NetworkEngine::onReadyTcpRead);

    UserInfo me;
    me.name = "Abonent";
    me.ip0 = "";
    me.ip1 = "";
    me.ip2 = "";
    me.lastSeen = QDateTime::currentDateTime();
    me.isAlive = true;
    m_users.append(me);

    readConfig();
    updateInterfaces();

    interfaceTimer = new QTimer(this);
    connect(interfaceTimer, &QTimer::timeout, this, &NetworkEngine::updateInterfaces);
    interfaceTimer->start(5000);

    discoveryTimer = new QTimer(this);
    connect(discoveryTimer, &QTimer::timeout, this, &NetworkEngine::sendDiscovery);
    discoveryTimer->start(10000);
}

NetworkEngine::~NetworkEngine() {}

void NetworkEngine::readConfig() {
    if (m_users.isEmpty()) return;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_users[0].name = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
    }
}

void NetworkEngine::updateInterfaces() {
    if (m_users.isEmpty()) return;
    QString oldIp0 = m_users[0].ip0;
    QString oldIp1 = m_users[0].ip1;
    QString oldIp2 = m_users[0].ip2;

    m_users[0].ip0 = "";
    m_users[0].ip1 = "";
    m_users[0].ip2 = "";

    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if ((interface.flags() & QNetworkInterface::IsUp) && !(interface.flags() & QNetworkInterface::IsLoopBack)) {
            for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    QString ipStr = entry.ip().toString();
                    if (ipStr.startsWith("192.168.")) {
                        if (ipStr.startsWith("192.168.43.")) {
                            m_users[0].ip1 = ipStr;
                        } else if (ipStr.startsWith("192.168.49.")) {
                            m_users[0].ip2 = ipStr;
                        } else {
                            m_users[0].ip0 = ipStr;
                        }
                    }
                }
            }
        }
    }

    if (oldIp0 != m_users[0].ip0 || oldIp1 != m_users[0].ip1 || oldIp2 != m_users[0].ip2) {
#if 0
        qDebug() << "**********************************************************************************";
        qDebug() << "**********************************************************************************";
        qDebug() << "**********************************************************************************";
        qDebug() << "**********************************************************************************";
        qDebug() << "=== Network Changed ===" << m_users[0].name << "LAN:" << m_users[0].ip0 << "AP:" << m_users[0].ip1 << "P2P:" << m_users[0].ip2;
        qDebug() << "**********************************************************************************";
        qDebug() << "**********************************************************************************";
        qDebug() << "**********************************************************************************";
#endif

        QJsonObject obj;
        obj["type"] = "discovery";
        obj["name"] = m_users[0].name;
        obj["ip0"] = m_users[0].ip0;
        obj["ip1"] = m_users[0].ip1;
        obj["ip2"] = m_users[0].ip2;
        QJsonDocument doc(obj);
        QByteArray data = doc.toJson(QJsonDocument::Compact);

        if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
            tcpClientSocket->write(data);
        }
        if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
            tcpSocket->write(data);
        }
    }
}

void NetworkEngine::sendDiscovery() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);

    QJsonObject obj;
    obj["type"] = "discovery";
    obj["name"] = me.name;
    obj["ip0"] = me.ip0;
    obj["ip1"] = me.ip1;
    obj["ip2"] = me.ip2;
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    if (!me.ip2.isEmpty() && me.ip2 != SERVER_IP) {
        udpSocket->writeDatagram(data, QHostAddress("192.168.49.255"), PORT);
        if (tcpSocket && tcpSocket->state() == QAbstractSocket::UnconnectedState) {
            tcpSocket->connectToHost(SERVER_IP, PORT);
        }
    }

    if (!me.ip0.isEmpty()) {
        udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), PORT);
    }
    if (!me.ip1.isEmpty()) {
        udpSocket->writeDatagram(data, QHostAddress("192.168.43.255"), PORT);
        udpSocket->writeDatagram(data, QHostAddress("192.168.137.255"), PORT);
    }

    if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
        sendUsersDataTcp(tcpClientSocket);
    }
    if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
        sendUsersDataTcp(tcpSocket);
    }
}
void NetworkEngine::onNewConnection() {
    tcpClientSocket = tcpServer->nextPendingConnection();
    connect(tcpClientSocket, &QTcpSocket::readyRead, this, &NetworkEngine::onReadyTcpRead);
}
void NetworkEngine::onReadyTcpRead() {
    QTcpSocket *senderSocket = qobject_cast<QTcpSocket*>(sender());
    if (!senderSocket) return;

    QByteArray data = senderSocket->readAll();
    QString ipStr = senderSocket->peerAddress().toString();
    parseIncomingSyncData(data, ipStr);
}

void NetworkEngine::readPendingDatagrams() {
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress senderIp;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &senderIp);

#if 0
        qDebug() << "readPendingDatagrams*************************************************************************";
        qDebug() << "TCP=" << data;
        qDebug() << "*************************************************************************";
#endif
        quint32 ipv4Int = senderIp.toIPv4Address();
        QString ipStr = (ipv4Int != 0) ? QHostAddress(ipv4Int).toString() : senderIp.toString();
        if (ipStr == "127.0.0.1" || ipStr == "::1") continue;

        parseIncomingSyncData(datagram, ipStr);
    }
}

void NetworkEngine::sendMessage(const QString &targetIp, const QString &message) {
    QJsonObject obj;
    obj["type"] = "message";
    obj["text"] = message;
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
        tcpClientSocket->write(data);
    } else if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
        tcpSocket->write(data);
    } else {
        udpSocket->writeDatagram(data, QHostAddress(targetIp), PORT);
    }
}

void NetworkEngine::startAudioCall(const QString &targetPeerName) {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QString targetIp = "";

    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].name == targetPeerName && m_users[i].isAlive) {
            if (!me.ip2.isEmpty() && !m_users[i].ip2.isEmpty()) {
                targetIp = m_users[i].ip2;
            } else if (!me.ip1.isEmpty() && !m_users[i].ip1.isEmpty()) {
                targetIp = m_users[i].ip1;
            } else if (!me.ip0.isEmpty() && !m_users[i].ip0.isEmpty()) {
                targetIp = m_users[i].ip0;
            }
            break;
        }
    }

    if (!targetIp.isEmpty()) {
        qDebug() << "=== Audio Call ===" << "Starting voice stream to:" << targetPeerName << "IP:" << targetIp;
        // Здесь ваше C++ ядро передаёт IP-адрес в AudioEngine для старта трансляции звука
        // audioEngine->startStream(targetIp, PORT);
    } else {
        qDebug() << "=== Audio Call ===" << "ERROR: Target peer IP not found for current network!";
    }
}

QVariantList NetworkEngine::activeUsers() const {
    QVariantList list;
    if (m_users.isEmpty()) return list;

    UserInfo me = m_users.at(0);

    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].isAlive) {
            QVariantMap map;
            map["name"] = m_users[i].name;

            QString targetIp = "";

            if (!me.ip2.isEmpty() && !m_users[i].ip2.isEmpty()) {
                targetIp = m_users[i].ip2;
            }
            else if (!me.ip1.isEmpty() && !m_users[i].ip1.isEmpty()) {
                targetIp = m_users[i].ip1;
            }
            else if (!me.ip0.isEmpty() && !m_users[i].ip0.isEmpty()) {
                targetIp = m_users[i].ip0;
            }

            if (!targetIp.isEmpty()) {
                map["ip"] = targetIp;
                list.append(map);
            }
        }
    }
    return list;
}

bool NetworkEngine::isRegistered() const { return true; }

QString NetworkEngine::getSavedName() const {
    if (!m_users.isEmpty()) return m_users.at(0).name;
    return QString("Abonent");
}
void NetworkEngine::sendUsersDataTcp(QTcpSocket *socket) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    if (m_users.size() < 1) return;

    QJsonObject rootObj;
    rootObj["type"] = "users_sync";

    QJsonArray userArray;
    for (int i = 0; i < m_users.size(); ++i) {
        QJsonObject userObj;
        userObj["name"] = m_users[i].name;
        userObj["ip0"] = m_users[i].ip0;
        userObj["ip1"] = m_users[i].ip1;
        userObj["ip2"] = m_users[i].ip2;
        userArray.append(userObj);
    }
    rootObj["users"] = userArray;

    QJsonDocument doc(rootObj);
    socket->write(doc.toJson(QJsonDocument::Compact));
#if 0
    qDebug() << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&";
    qDebug() <<   doc.toJson(QJsonDocument::Compact);
    qDebug() << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&";
#endif
}
void NetworkEngine::debugUsers() const {
    qDebug() << "========================================";
    qDebug() << "=== TELELOC CURRENT ACTIVE USERS LIST ===";
    for (int i = 0; i < m_users.size(); ++i) {
        QString prefix = (i == 0) ? "[ME] " : "[PEER] ";
        qDebug() << prefix << "Name:" << m_users[i].name
                 << " | LAN(ip0):" << (m_users[i].ip0.isEmpty() ? "EMPTY" : m_users[i].ip0)
                 << " | AP(ip1):" << (m_users[i].ip1.isEmpty() ? "EMPTY" : m_users[i].ip1)
                 << " | P2P(ip2):" << (m_users[i].ip2.isEmpty() ? "EMPTY" : m_users[i].ip2)
                 << " | Alive:" << m_users[i].isAlive;
    }
    qDebug() << "========================================";
}

QVariantList NetworkEngine::getUsers(int netType) const {
    QVariantList list;
    if (m_users.isEmpty()) return list;

    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].isAlive) {
            QString targetIp = "";

            if (netType == 0 && !m_users[i].ip0.isEmpty()) {
                targetIp = m_users[i].ip0;
            } else if (netType == 1 && !m_users[i].ip1.isEmpty()) {
                targetIp = m_users[i].ip1;
            } else if (netType == 2 && !m_users[i].ip2.isEmpty()) {
                targetIp = m_users[i].ip2;
            }

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

    if (type == "discover<y") {
        QString name = obj["name"].toString();
        QString ip0 = obj["ip0"].toString();
        QString ip1 = obj["ip1"].toString();
        QString ip2 = obj["ip2"].toString();
        qDebug() <<  "ЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖ" << name << ip0 << ip1 << ip2 ;
        bool found = false;

        for (int i = 1; i < m_users.size(); ++i) {
            if ((!ip2.isEmpty() && m_users[i].ip2 == ip2) || (!ip1.isEmpty() && m_users[i].ip1 == ip1) || (!ip0.isEmpty() && m_users[i].ip0 == ip0)) {
                m_users[i].name = name;
                m_users[i].ip0 = ip0;
                m_users[i].ip1 = ip1;
                m_users[i].ip2 = ip2;
                m_users[i].lastSeen = QDateTime::currentDateTime();
                m_users[i].isAlive = true;
                found = true;
                break;
            }
        }

        if (!found) {
            UserInfo u;
            u.name = name;
            u.ip0 = ip0;
            u.ip1 = ip1;
            u.ip2 = ip2;
            u.lastSeen = QDateTime::currentDateTime();
            u.isAlive = true;
            m_users.append(u);
            emit activeUsersChanged();
        }

        UserInfo me = m_users.at(0);
        if (!me.ip2.isEmpty() && me.ip2 == SERVER_IP) {
            UserInfo me = m_users.at(0);
            if (!me.ip2.isEmpty() && me.ip2 == SERVER_IP) {
                if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
                    sendUsersDataTcp(tcpClientSocket);
                } else {
                    if (!tcpSocket) {
                        tcpSocket = new QTcpSocket(this);
                        connect(tcpSocket, &QTcpSocket::readyRead, this, &NetworkEngine::onReadyTcpRead);
                    }

                    if (tcpSocket->state() == QAbstractSocket::ConnectedState && tcpSocket->peerAddress().toString() == ip2) {
                        sendUsersDataTcp(tcpSocket);
                    } else if (tcpSocket->state() != QAbstractSocket::ConnectingState && tcpSocket->state() != QAbstractSocket::ConnectedState) {
                        tcpSocket->abort();

                        // Ловим момент железобетонного коннекта и сразу шлём данные Петру
                        connect(tcpSocket, &QTcpSocket::connected, this, [this]() {
                                sendUsersDataTcp(tcpSocket);
                            }, Qt::UniqueConnection);

                        tcpSocket->connectToHost(ip2, PORT);
                    }
                }
            }

        }
    } else if (type == "users_sync") {
        QJsonArray userArray = obj["users"].toArray();
        for (int i = 0; i < userArray.size(); ++i) {
            QJsonObject uObj = userArray[i].toObject();
            QString name = uObj["name"].toString();
            QString ip0 = uObj["ip0"].toString();
            QString ip1 = uObj["ip1"].toString();
            QString ip2 = uObj["ip2"].toString();

            if (!ip2.isEmpty() && ip2 == m_users[0].ip2) continue;
            if (!ip1.isEmpty() && ip1 == m_users[0].ip1) continue;
            if (!ip0.isEmpty() && ip0 == m_users[0].ip0) continue;

            bool found = false;
            for (int j = 1; j < m_users.size(); ++j) {
                if ((!ip2.isEmpty() && m_users[j].ip2 == ip2) || (!ip1.isEmpty() && m_users[j].ip1 == ip1) || (!ip0.isEmpty() && m_users[j].ip0 == ip0)) {
                    m_users[j].name = name;
                    m_users[j].ip0 = ip0;
                    m_users[j].ip1 = ip1;
                    m_users[j].ip2 = ip2;
                    m_users[j].lastSeen = QDateTime::currentDateTime();
                    m_users[j].isAlive = true;
                    found = true;
                    break;
                }
            }
            if (!found) {
                UserInfo u;
                u.name = name;
                u.ip0 = ip0;
                u.ip1 = ip1;
                u.ip2 = ip2;
                u.lastSeen = QDateTime::currentDateTime();
                u.isAlive = true;
                m_users.append(u);
                emit activeUsersChanged();
            }
        }
    } else if (type == "message") {
        emit messageReceived(senderIpStr, obj["text"].toString());
    }
}

void NetworkEngine::startWifiDirectScan() {}
void NetworkEngine::connectToWifiDirectDevice(const QString &) {}
void NetworkEngine::tryConnectToMaster() {}
void NetworkEngine::createAndroidP2pGroup() {}
QVariantList NetworkEngine::p2pPeers() const { return m_p2pPeersList; }
