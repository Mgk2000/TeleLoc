#include "networkengine.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>

NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent), tcpClientSocket(nullptr), m_activeCallNetType(-1), m_isCallActive(false) {

    m_ringbackTone = new QSoundEffect(this);
    m_ringbackTone->setSource(QUrl("qrc:/qt/qml/TeleLoc/ringback.wav"));
    m_ringbackTone->setLoopCount(QSoundEffect::Infinite);

    m_busyTone = new QSoundEffect(this);
    m_busyTone->setSource(QUrl("qrc:/qt/qml/TeleLoc/busy.wav"));
    m_busyTone->setLoopCount(5);

    m_incomingRing = new QSoundEffect(this);
    m_incomingRing->setSource(QUrl("qrc:/qt/qml/TeleLoc/ringtone.wav"));
    m_incomingRing->setLoopCount(QSoundEffect::Infinite);

    UserInfo me;
    me.name = "Пользователь";
    me.isAlive = true;
    m_users.append(me);

    udpSocket = new QUdpSocket(this);
    udpSocket->bind(PORT, QUdpSocket::ShareAddress);
    connect(udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::onReadyUdpRead);

    tcpServer = new QTcpServer(this);
    connect(tcpServer, &QTcpServer::newConnection, this, &NetworkEngine::onNewConnection);

    // C++ ядро слушает свой порт 28000 на всех платформах
    tcpServer->listen(QHostAddress::Any, PORT);

    tcpSocket = new QTcpSocket(this);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &NetworkEngine::onReadyTcpRead);

    audioEngine = new AudioEngine(this);

    readConfig();
    updateInterfaces();

    QTimer *interfaceTimer = new QTimer(this);
    connect(interfaceTimer, &QTimer::timeout, this, &NetworkEngine::updateInterfaces);
    interfaceTimer->start(10000);

    QTimer *discoveryTimer = new QTimer(this);
    connect(discoveryTimer, &QTimer::timeout, this, &NetworkEngine::sendDiscovery);
    discoveryTimer->start(10000);
}

NetworkEngine::~NetworkEngine() {
    if (m_isCallActive) {
        audioEngine->stop();
    }
}

void NetworkEngine::handleVoipWakeup(const QString &callerName) {
    emit messageReceived("СИСТЕМА", "Приложение разбужено Java-интентом! Вызов от: " + callerName);
    m_incomingRing->play();
    emit incomingCall(callerName, 0);
}

bool NetworkEngine::isNetTypeAvailable(int netType) {
    if (m_users.isEmpty()) return false;
    UserInfo me = m_users.at(0);
    if (netType == 0) return !me.ip0.isEmpty();
    if (netType == 1) return !me.ip1.isEmpty();
    if (netType == 2) return !me.ip2.isEmpty();
    return false;
}

QStringList NetworkEngine::getUsers(int netType) {
    QStringList list;
    if (netType == -1) {
        int peersCount = 0;
        for (int i = 1; i < m_users.size(); ++i) {
            if (m_users[i].isAlive) {
                if (!m_users[i].ip0.isEmpty() || !m_users[i].ip1.isEmpty() || !m_users[i].ip2.isEmpty()) {
                    list.append(m_users[i].name);
                    peersCount++;
                }
            }
        }
        if (peersCount > 1) {
            list.append("Все");
        }
        return list;
    }
    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].isAlive) {
            if (netType == 0 && !m_users[i].ip0.isEmpty()) list.append(m_users[i].name);
            else if (netType == 1 && !m_users[i].ip1.isEmpty()) list.append(m_users[i].name);
            else if (netType == 2 && !m_users[i].ip2.isEmpty()) list.append(m_users[i].name);
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

    int currentNetType = -1;
    if (senderIpStr.startsWith("192.168.49.")) currentNetType = 2;
    else if (senderIpStr.startsWith("192.168.43.") || senderIpStr.startsWith("192.168.137.")) currentNetType = 1;
    else currentNetType = 0;

    int peerIndex = -1;
    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].name == name) {
            peerIndex = i;
            break;
        }
    }

    if (peerIndex == -1) {
        UserInfo newUser;
        newUser.name = name;
        newUser.isAlive = true;
        if (currentNetType == 0) newUser.ip0 = senderIpStr;
        else if (currentNetType == 1) newUser.ip1 = senderIpStr;
        else if (currentNetType == 2) newUser.ip2 = senderIpStr;
        m_users.append(newUser);
        peerIndex = m_users.size() - 1;
    } else {
        m_users[peerIndex].isAlive = true;
        if (currentNetType == 0) m_users[peerIndex].ip0 = senderIpStr;
        else if (currentNetType == 1) m_users[peerIndex].ip1 = senderIpStr;
        else if (currentNetType == 2) m_users[peerIndex].ip2 = senderIpStr;
    }

    if (type == "incoming_call") {
        int netType = obj["net_type"].toInt();
        if (m_isCallActive) {
            QJsonObject busyObj;
            busyObj["type"] = "line_busy";
            busyObj["name"] = m_users.at(0).name;
            QByteArray busyData = QJsonDocument(busyObj).toJson(QJsonDocument::Compact);
            if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
                tcpClientSocket->write(busyData);
            } else if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
                tcpSocket->write(busyData);
            }
            return;
        }
        m_activePeerIp = senderIpStr;
        m_activeCallNetType = netType;
        m_incomingRing->play();
        emit incomingCall(name, netType);
    }
    else if (type == "accept_call") {
        m_ringbackTone->stop();
        m_isCallActive = true;
        for (int i = 1; i < m_users.size(); ++i) {
            if (m_users[i].name == name && m_users[i].isAlive) {
                if (m_activeCallNetType == 0) m_activePeerIp = m_users[i].ip0;
                else if (m_activeCallNetType == 1) m_activePeerIp = m_users[i].ip1;
                else if (m_activeCallNetType == 2) m_activePeerIp = m_users[i].ip2;
                break;
            }
        }
        audioEngine->startRecording(m_activePeerIp);
        emit callAccepted();
    }
    else if (type == "line_busy") {
        m_ringbackTone->stop();
        m_busyTone->play();
        m_activePeerIp = "";
        m_activeCallNetType = -1;
        emit callStopped();
    }
    else if (type == "stop_call") {
        m_ringbackTone->stop();
        m_incomingRing->stop();
        m_busyTone->stop();
        audioEngine->stop();
        m_isCallActive = false;
        m_activePeerIp = "";
        m_activeCallNetType = -1;
        emit callStopped();
    }
    else if (type == "message") {
        emit messageReceived(name, obj["text"].toString());
    }
    else if (type == "discovery") {
        emit peerListChanged();
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

    if (targetIp.startsWith("::ffff:")) {
        targetIp.remove("::ffff:");
    }

    if (!targetIp.isEmpty()) {
        m_activePeerIp = targetIp;
        m_activeCallNetType = netType;

        QJsonObject obj;
        obj["type"] = "incoming_call";
        obj["name"] = me.name;
        obj["net_type"] = netType;

        // КРИТИЧЕСКИ ВАЖНО: Добавляем \n для Java-парсерa Ивана!
        QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";

        emit messageReceived("СИСТЕМА", "ВЫСТРЕЛ ВЫЗОВА ДЛЯ " + targetPeerName + " НА IP: " + targetIp);

        // ВЫСТРЕЛ 1: Десктопный C++ порт 28000
        QTcpSocket socket28000;
        socket28000.connectToHost(targetIp, PORT); // PORT = 28000
        if (socket28000.waitForConnected(300)) {
            socket28000.write(data);
            socket28000.waitForBytesWritten(300);
            socket28000.disconnectFromHost();
        }

        // ВЫСТРЕЛ 2: Направляем короткий TCP прямо в работающий Java-будильник Ивана (28500)
        QTcpSocket socket28500;
        socket28500.connectToHost(targetIp, 28500);
        if (socket28500.waitForConnected(300)) {
            socket28500.write(data);
            socket28500.waitForBytesWritten(300);
            socket28500.disconnectFromHost(); // Закрываем сразу, как в чате!
            emit messageReceived("СИСТЕМА", "Доставлено на Java-порт 28500");
        }

        m_ringbackTone->play();
    } else {
        emit messageReceived("СИСТЕМА", "ОШИБКА: НЕ НАЙДЕН IP");
    }
}
void NetworkEngine::acceptAudioCall(const QString &targetPeerName, int netType) {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    m_incomingRing->stop();
    m_isCallActive = true;
    QJsonObject obj;
    obj["type"] = "accept_call";
    obj["name"] = me.name;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QString targetIp = "";
    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].name == targetPeerName && m_users[i].isAlive) {
            if (netType == 0) targetIp = m_users[i].ip0;
            else if (netType == 1) targetIp = m_users[i].ip1;
            else if (netType == 2) targetIp = m_users[i].ip2;
            break;
        }
    }

    if (targetIp.startsWith("::ffff:")) {
        targetIp.remove("::ffff:");
    }

    if (!targetIp.isEmpty()) {
        m_activePeerIp = targetIp;
        m_activeCallNetType = netType;
        tcpSocket->abort();
        tcpSocket->connectToHost(targetIp, PORT);
        if (tcpSocket->waitForConnected(1200)) {
            tcpSocket->write(data);
            tcpSocket->waitForBytesWritten(500);
        }
    }

    audioEngine->startRecording(m_activePeerIp);
}

void NetworkEngine::stopAudioCall() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QJsonObject obj;
    obj["type"] = "stop_call";
    obj["name"] = me.name;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QString targetIp = m_activePeerIp;
    if (targetIp.startsWith("::ffff:")) {
        targetIp.remove("::ffff:");
    }

    if (!targetIp.isEmpty() && tcpSocket) {
        tcpSocket->abort();
        tcpSocket->connectToHost(targetIp, PORT);
        if (tcpSocket->waitForConnected(500)) {
            tcpSocket->write(data);
            tcpSocket->waitForBytesWritten(500);
        }
    }

    m_ringbackTone->stop();
    m_incomingRing->stop();
    m_busyTone->stop();
    audioEngine->stop();
    m_isCallActive = false;
    m_activePeerIp = "";
    m_activeCallNetType = -1;
    emit callStopped();
}

void NetworkEngine::sendMessage(const QString &targetPeer, const QString &text) {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QJsonObject obj;
    obj["type"] = "message";
    obj["name"] = me.name;
    obj["text"] = text;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    if (targetPeer == "Все") {
        bool udpSent = false;
        if (!me.ip0.isEmpty()) {
            udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), PORT);
            udpSent = true;
        }
        if (!me.ip1.isEmpty()) {
            udpSocket->writeDatagram(data, QHostAddress("192.168.43.255"), PORT);
            udpSocket->writeDatagram(data, QHostAddress("192.168.137.255"), PORT);
            udpSent = true;
        }
        if (!udpSent || !me.ip2.isEmpty()) {
            for (int i = 1; i < m_users.size(); ++i) {
                if (m_users[i].isAlive && !m_users[i].ip2.isEmpty()) {
                    QTcpSocket tmpSocket;
                    tmpSocket.connectToHost(m_users[i].ip2, PORT);
                    if (tmpSocket.waitForConnected(500)) {
                        tmpSocket.write(data);
                        tmpSocket.waitForBytesWritten(500);
                    }
                }
            }
        }
        return;
    }
    QString targetIp = "";
    for (int i = 1; i < m_users.size(); ++i) {
        if (m_users[i].name == targetPeer && m_users[i].isAlive) {
            if (!m_users[i].ip0.isEmpty() && !me.ip0.isEmpty()) targetIp = m_users[i].ip0;
            else if (!m_users[i].ip1.isEmpty() && !me.ip1.isEmpty()) targetIp = m_users[i].ip1;
            else if (!m_users[i].ip2.isEmpty() && !me.ip2.isEmpty()) targetIp = m_users[i].ip2;
            break;
        }
    }
    if (!targetIp.isEmpty()) {
        QTcpSocket tmpSocket;
        tmpSocket.connectToHost(targetIp, PORT);
        if (tmpSocket.waitForConnected(1000)) {
            tmpSocket.write(data);
            tmpSocket.waitForBytesWritten(500);
        }
    }
}

void NetworkEngine::sendDiscovery() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QJsonObject obj;
    obj["type"] = "discovery";
    obj["name"] = me.name;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), PORT);
    udpSocket->writeDatagram(data, QHostAddress("192.168.43.255"), PORT);
    udpSocket->writeDatagram(data, QHostAddress("192.168.137.255"), PORT);
}

void NetworkEngine::updateInterfaces() {
    if (m_users.isEmpty()) return;
    m_users[0].ip0 = "";
    m_users[0].ip1 = "";
    m_users[0].ip2 = "";
    QList<QHostAddress> list = QNetworkInterface::allAddresses();
    for (int i = 0; i < list.count(); i++) {
        if (!list[i].isLoopback() && list[i].protocol() == QAbstractSocket::IPv4Protocol) {
            QString ip = list[i].toString();
            if (ip.startsWith("192.168.49.")) m_users[0].ip2 = ip;
            else if (ip.startsWith("192.168.43.") || ip.startsWith("192.168.137.")) m_users[0].ip1 = ip;
            else m_users[0].ip0 = ip;
        }
    }
    m_users[0].name = getSavedName();
}

void NetworkEngine::readConfig() {
    if (m_users.isEmpty()) return;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_users[0].name = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
    }
}

QString NetworkEngine::getSavedName() {
    if (!m_users.isEmpty()) {
        return m_users[0].name;
    }
    return "Пользователь";
}

void NetworkEngine::saveNameToFile(const QString &name) {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    QFile file(path + "/teleloc.conf");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(name.trimmed().toUtf8());
        file.close();
    }
    if (!m_users.isEmpty()) {
        m_users[0].name = name.trimmed();
        emit peerListChanged();
    }
}

void NetworkEngine::onNewConnection() {
    if (tcpClientSocket) {
        tcpClientSocket->disconnectFromHost();
        tcpClientSocket->deleteLater();
    }
    tcpClientSocket = tcpServer->nextPendingConnection();
    connect(tcpClientSocket, &QTcpSocket::readyRead, this, &NetworkEngine::onReadyTcpRead);
}

void NetworkEngine::onReadyTcpRead() {
    QTcpSocket *senderSocket = qobject_cast<QTcpSocket*>(sender());
    if (!senderSocket) return;
    QByteArray data = senderSocket->readAll();
    parseIncomingSyncData(data, senderSocket->peerAddress().toString());
}

void NetworkEngine::onReadyUdpRead() {
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress senderHost;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &senderHost);
        parseIncomingSyncData(datagram, senderHost.toString());
    }
}

void NetworkEngine::debugUsers() {
    for (int i = 0; i < m_users.size(); ++i) {
        qDebug() << "User:" << m_users[i].name << "LAN:" << m_users[i].ip0 << "AP:" << m_users[i].ip1 << "Direct:" << m_users[i].ip2 << "Alive:" << m_users[i].isAlive;
    }
}
