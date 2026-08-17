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
#include <QJsonArray>
#include <QJsonValue>
#include <QThread>

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

#ifndef Q_OS_ANDROID
    // UDP-приемник на порту 28000 инициализируется и слушает сеть ТОЛЬКО на Windows
    udpSocket = new QUdpSocket(this);
    if (udpSocket->bind(QHostAddress::AnyIPv4, 28000, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        connect(udpSocket, &QUdpSocket::readyRead, this, &NetworkEngine::onReadyUdpRead);
        qDebug() << "@@@ СЕТЬ C++: Приемник Discovery успешно запущен на порту 28000 (Платформа без Android)";
    }
#else
    qDebug() << "@@@ СЕТЬ C++: На Android прием UDP 28000 полностью отключен. Этим занимается Java-служба.";
#endif

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
#ifndef Q_OS_ANDROID
    QTimer *discoveryTimer = new QTimer(this);
    connect(discoveryTimer, &QTimer::timeout, this, &NetworkEngine::sendDiscovery);
    discoveryTimer->start(10000);
#endif
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
    readConfig();
    qDebug() << "@@@@ abonents от net" << netType << "users=" << m_users.size();
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
        qDebug() << "user[" << i <<"]=" << &m_users[i].name << m_users[i].ip0 << m_users[i].isAlive;
        if (m_users[i].isAlive) {
            if (netType == 0 && !m_users[i].ip0.isEmpty()) list.append(m_users[i].name);
            else if (netType == 1 && !m_users[i].ip1.isEmpty()) list.append(m_users[i].name);
            else if (netType == 2 && !m_users[i].ip2.isEmpty()) list.append(m_users[i].name);
        }
    }
    return list;
}
void NetworkEngine::parseIncomingSyncData(const QByteArray &data, const QString &senderIpStr) {
#ifndef Q_OS_ANDROID

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

    bool needSaveConfig = false;

    if (peerIndex == -1) {
        UserInfo newUser;
        newUser.name = name;
        newUser.isAlive = true;
        if (currentNetType == 0) newUser.ip0 = senderIpStr;
        else if (currentNetType == 1) newUser.ip1 = senderIpStr;
        else if (currentNetType == 2) newUser.ip2 = senderIpStr;
        m_users.append(newUser);
        peerIndex = m_users.size() - 1;
        needSaveConfig = true; // Появился новый друг — фиксируем в JSON!
    } else {
        m_users[peerIndex].isAlive = true;

        // Проверяем, изменился ли реальный IP-адрес пира на данном интерфейсе
        if (currentNetType == 0 && m_users[peerIndex].ip0 != senderIpStr) {
            m_users[peerIndex].ip0 = senderIpStr;
            needSaveConfig = true;
        }
        else if (currentNetType == 1 && m_users[peerIndex].ip1 != senderIpStr) {
            m_users[peerIndex].ip1 = senderIpStr;
            needSaveConfig = true;
        }
        else if (currentNetType == 2 && m_users[peerIndex].ip2 != senderIpStr) {
            m_users[peerIndex].ip2 = senderIpStr;
            needSaveConfig = true;
        }
    }

    // Если зафиксировано изменение сетевых адресов — мгновенно обновляем наш единый конфиг на диске
    if (needSaveConfig) {
        savePeersToConfig();
        emit peerListChanged();
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
#endif
}
void NetworkEngine::startAudioCall(const QString &targetPeerName, int netType) {
    qDebug() << "@@@ СЕТЬ C++: Вызов startAudioCall() для:" << targetPeerName;

    QString targetIp = "";
    QList<UserInfo> updatedPeers = loadPeersFromConfig();

    for (const UserInfo &peer : updatedPeers) {
        if (peer.name == targetPeerName) {
            targetIp = peer.ip0;
            break;
        }
    }

    if (targetIp.isEmpty()) {
        qDebug() << "@@@ СЕТЬ C++ ОШИБКА: Не найден IP-адрес для пользователя:" << targetPeerName;
        emit messageReceived("СИСТЕМА", "Ошибка: IP для " + targetPeerName + " не найден в конфиге.");
        return;
    }

    m_activePeerIp = targetIp;
    m_activeCallNetType = netType;

    QJsonObject obj;
    obj["type"] = "incoming_call";
    obj["name"] = getSavedName();
    obj["net_type"] = QString::number(netType);
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";

    tcpSocket->abort();
    tcpSocket->connectToHost(targetIp, 28500);

    if (tcpSocket->waitForConnected(3000)) {
        tcpSocket->write(data);
        tcpSocket->waitForBytesWritten(1000);
        tcpSocket->disconnectFromHost();
        qDebug() << "@@@ СЕТЬ C++: Пакет вызова успешно отправлен на Java-сервер порта 28500";
    } else {
        qDebug() << "@@@ СЕТЬ C++ ОШИБКА: Порт 28500 не ответил. Пробую резервный порт C++:" << PORT;
        tcpSocket->abort();
        tcpSocket->connectToHost(targetIp, PORT);
        if (tcpSocket->waitForConnected(2000)) {
            tcpSocket->write(data);
            tcpSocket->waitForBytesWritten(1000);
            tcpSocket->disconnectFromHost();
        }
    }
    m_ringbackTone->play();
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

void NetworkEngine::readConfig() {
    if (m_users.isEmpty()) return;

    QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf");
    if (!file.open(QIODevice::ReadOnly)) {
        m_users[0].name = "Пользователь"; // Дефолтное имя, если файла еще нет
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        m_users[0].name = "Пользователь";
        return;
    }

    QJsonObject rootObj = doc.object();

    // 1. Восстанавливаем имя владельца рации
    if (rootObj.contains("my_name")) {
        m_users[0].name = rootObj["my_name"].toString().trimmed();
    } else {
        m_users[0].name = "Пользователь";
    }

    // 2. Восстанавливаем кэшированных друзей (Петра, Марью и т.д.)
    if (rootObj.contains("peers") && rootObj["peers"].isArray()) {
        QJsonArray peersArray = rootObj["peers"].toArray();
        for (int i = 0; i < peersArray.size(); ++i) {
            QJsonObject peerObj = peersArray.at(i).toObject();
            QString peerName = peerObj["name"].toString();

            if (peerName.isEmpty() || peerName == m_users.at(0).name) continue;

            // Проверяем дубликаты на всякий случай
            bool exists = false;
            for (int j = 1; j < m_users.size(); ++j) {
                if (m_users[j].name == peerName) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                UserInfo savedUser;
                savedUser.name = peerName;
//                savedUser.isAlive = false; // Они пока оффлайн, но в списке Петр ПОЯВИТСЯ!
                savedUser.isAlive = peerObj["isAlive"].toBool();
                savedUser.ip0 = peerObj["ip0"].toString();
                savedUser.ip1 = peerObj["ip1"].toString();
                savedUser.ip2 = peerObj["ip2"].toString();
                m_users.append(savedUser);
            }
        }
    }
    emit peerListChanged();
}

void NetworkEngine::saveNameToFile(const QString &name) {
    if (m_users.isEmpty()) return;
    m_users[0].name = name.trimmed();

    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);

    // Читаем старый файл, чтобы не затереть существующих пиров при смене имени
    QJsonObject rootObj;
    QFile readFile(path + "/teleloc.conf");
    if (readFile.open(QIODevice::ReadOnly)) {
        QJsonDocument readDoc = QJsonDocument::fromJson(readFile.readAll());
        if (!readDoc.isNull() && readDoc.isObject()) {
            rootObj = readDoc.object();
        }
        readFile.close();
    }

    // Перезаписываем только имя владельца
    rootObj["my_name"] = m_users[0].name;

    QFile writeFile(path + "/teleloc.conf");
    if (writeFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(rootObj);
        writeFile.write(doc.toJson(QJsonDocument::Compact));
        writeFile.close();
    }
    emit peerListChanged();
}

QString NetworkEngine::getSavedName() {
    if (!m_users.isEmpty()) {
        return m_users[0].name;
    }
    return "Пользователь";
}

void NetworkEngine::savePeersToConfig() {
    if (m_users.isEmpty()) return;

    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QJsonObject rootObj;

    // Сначала читаем текущее имя из файла, чтобы не потерять его
    QFile readFile(path + "/teleloc.conf");
    if (readFile.open(QIODevice::ReadOnly)) {
        QJsonDocument readDoc = QJsonDocument::fromJson(readFile.readAll());
        if (!readDoc.isNull() && readDoc.isObject()) {
            rootObj = readDoc.object();
        }
        readFile.close();
    }

    if (!rootObj.contains("my_name")) {
        rootObj["my_name"] = m_users[0].name;
    }

    // Сериализуем всех друзей из оперативной памяти в JSON-массив
    QJsonArray peersArray;
    for (int i = 1; i < m_users.size(); ++i) {
        QJsonObject peerObj;
        peerObj["name"] = m_users[i].name;
        peerObj["ip0"] = m_users[i].ip0;
        peerObj["ip1"] = m_users[i].ip1;
        peerObj["ip2"] = m_users[i].ip2;
        peersArray.append(peerObj);
    }
    rootObj["peers"] = peersArray;

    QFile writeFile(path + "/teleloc.conf");
    if (writeFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(rootObj);
        writeFile.write(doc.toJson(QJsonDocument::Compact));
        writeFile.close();
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

#ifndef Q_OS_ANDROID
void NetworkEngine::sendDiscovery() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QJsonObject obj;
    obj["type"] = "discovery";
    obj["name"] = me.name;
    obj["ip0"] = me.ip0;
    obj["ip1"] = me.ip1;
    obj["ip2"] = me.ip2;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    qDebug() << "@@@@@@@@@@@@@NetworkEngine::sendDiscovery" << QString(data);
    udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), PORT);
    udpSocket->writeDatagram(data, QHostAddress("192.168.43.255"), PORT);
    udpSocket->writeDatagram(data, QHostAddress("192.168.137.255"), PORT);
}
#endif
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
        QString s (datagram);
        qDebug() << "@@@@@@@@@@@@@@@@@@@@@onReadyUdpRead" << s;
        parseIncomingSyncData(datagram, senderHost.toString());
    }
}

void NetworkEngine::debugUsers() {
    for (int i = 0; i < m_users.size(); ++i) {
        qDebug() << "User:" << m_users[i].name << "LAN:" << m_users[i].ip0 << "AP:" << m_users[i].ip1 << "Direct:" << m_users[i].ip2 << "Alive:" << m_users[i].isAlive;
    }
}
#ifdef Q_OS_ANDROID
#include <jni.h>

extern "C" JNIEXPORT void JNICALL
Java_org_qtproject_example_appteleloc_TeleLocActivity_triggerCppWakeup(JNIEnv *env, jobject thiz, jstring name) {
    Q_UNUSED(thiz);
    const char *nativeString = env->GetStringUTFChars(name, nullptr);
    if (nativeString) {
        QString callerName = QString::fromUtf8(nativeString);
        env->ReleaseStringUTFChars(name, nativeString);

        // Напрямую вызываем открытие QML-окна вызова
        QMetaObject::invokeMethod(qApp, [callerName]() {
                // Находим наш запущенный NetworkEngine в контексте приложения
                NetworkEngine *engine = qobject_cast<NetworkEngine*>(qApp->property("netEngine").value<QObject*>());
                if (engine) {
                    engine->handleVoipWakeup(callerName);
                }
            }, Qt::QueuedConnection);
    }
}
#endif
QList<UserInfo> NetworkEngine::loadPeersFromConfig() {
    QList<UserInfo> activeUsers;
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf";
    QFile file(configPath);

    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (doc.isObject()) {
            QJsonObject configObj = doc.object();
            QJsonArray peersArray = configObj["peers"].toArray();

            for (const QJsonValue &value : peersArray) {
                QJsonObject peerObj = value.toObject();
                QString name = peerObj["name"].toString();
                QString ip0 = peerObj["ip0"].toString();

                if (!name.isEmpty() && !ip0.isEmpty()) {
                    UserInfo u;
                    u.name = name;
                    u.ip0 = ip0;
                    u.ip1 = peerObj["ip1"].toString();
                    u.ip2 = peerObj["ip2"].toString();
                    u.isAlive = true;
                    activeUsers.append(u);
                }
            }
        }
    }
    return activeUsers;
}
void NetworkEngine::refreshPeersForUi() {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf";
    QFile file(configPath);

    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        return;
    }

    QJsonObject configObj = doc.object();
    QJsonArray peersArray = configObj["peers"].toArray();

    m_users.clear();

    for (const QJsonValue &value : peersArray) {
        QJsonObject peerObj = value.toObject();
        QString name = peerObj["name"].toString();
        QString ip0 = peerObj["ip0"].toString();

        if (!name.isEmpty() && !ip0.isEmpty()) {
            UserInfo u;
            u.name = name;
            u.ip0 = ip0;
            u.ip1 = peerObj["ip1"].toString();
            u.ip2 = peerObj["ip2"].toString();
            u.isAlive = true;
            m_users.append(u);
        }
    }

    emit usersModelChanged();
}
