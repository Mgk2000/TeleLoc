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
#include <QLocalServer>
#include <QLocalSocket>
#include <QGuiApplication>

NetworkEngine * netEngine;
NetworkEngine::NetworkEngine(QObject *parent)
    : QObject(parent), tcpClientSocket(nullptr) {
    qDebug() << "@@@ NetworkEngine::NetworkEngine 0";
    netEngine = this;
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
    qDebug() << "@@@ NetworkEngine::NetworkEngine 1";

#ifndef Q_OS_ANDROID1
    // UDP-приемник на порту 28001 инициализируется и слушает сеть ТОЛЬКО на Windows
    udpSocket = new QUdpSocket(this);
    if (udpSocket->bind(QHostAddress::AnyIPv4, 28001, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
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
    qDebug() << "@@@ NetworkEngine::NetworkEngine 2";

    readConfig();
    qDebug() << "@@@ NetworkEngine::NetworkEngine 3";
    updateInterfaces();

    QTimer *interfaceTimer = new QTimer(this);
    connect(interfaceTimer, &QTimer::timeout, this, &NetworkEngine::updateInterfaces);
    qDebug() << "@@@ NetworkEngine::NetworkEngine 4";
    interfaceTimer->start(10000);
#ifndef Q_OS_ANDROID
    QTimer *discoveryTimer = new QTimer(this);
    connect(discoveryTimer, &QTimer::timeout, this, &NetworkEngine::sendDiscovery);
    discoveryTimer->start(10000);
#else
    m_unixServer = nullptr;
    m_unixClientSocket = nullptr;
    m_unixAliveTimer = nullptr;

    // Запускаем локальный Unix-сервер
    m_unixStartServer();
    qDebug() << "@@@ NetworkEngine::NetworkEngine 5";
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, [](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            // ПРИЛОЖЕНИЕ ТОЛЬКО ЧТО РАЗВЕРНУЛОСЬ НА ЭКРАН!
            qDebug() << "Процесс разбужен и активен. Проверяем файл звонка...";

            // Вызываем вашу функцию проверки файла
            netEngine->readConfig();
        }
    });


#endif

}

NetworkEngine::~NetworkEngine() {
    if (callInfo.state == CallInfo::speaking) {
        audioEngine->stop();
    }
}
void NetworkEngine::sendCallByTcp(const QString &name, int netType) {
    if (m_users.isEmpty()) return;
    QByteArray data = getCallData(netType);
    qDebug() << "@@@@@@@@@@@@@NetworkEngine::sendCall to" << name;
    //   qDebug() << "callip=" << callIp << udpSocket << QHostAddress(callIp);
    //    udpSocket->writeDatagram(data, QHostAddress(callIp), PORT);
    //    udpSocket->writeDatagram(data, QHostAddress(callIp), PORT);
    //    udpSocket->reset();
    //    qint64 nb = udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), PORT);
    //udpSocket->flush();
    //    qDebug() << "@@@ Send" << nb << "bytes";
    m_ringbackTone->play();
    QTcpSocket *socket = new QTcpSocket();
    if (m_users.isEmpty()) return;
    QString targetIp;
    for (int i =1; i<m_users.count(); i++)
        if (m_users[i].name == name)
        {
            targetIp = m_users[i].ip[netType];
            break;
        }
    int port = 28501;
    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    socket->connectToHost(targetIp, port);

    if (socket->waitForConnected(3000)) {
        // Форматируем данные так же, как их ждет Java-сервис
       // QString payload = QString("%1;%2").arg(callerName, callerIp);

        socket->write(data);
        socket->flush();
        socket->disconnectFromHost();
        qDebug() << "@@@ Send call by TCP to" << name;
    } else {
        qDebug() << "Ошибка вызова по TCP:" << socket->errorString();
        socket->deleteLater();
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
    return !me.ip[netType].isEmpty();
}

QStringList NetworkEngine::getUsers(int netType) {
    readConfig();
    qDebug() << "@@@@ abonents от net" << netType << "users=" << m_users.size();
    QStringList list;
    if (netType == -1) {
        int peersCount = 0;
        for (int i = 1; i < m_users.size(); ++i) {
           if (m_users[i].isAlive) {
               if (!m_users[i].ip[0].isEmpty() ||
               !m_users[i].ip[1].isEmpty() ||
               !m_users[i].ip[2].isEmpty()) {
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
        qDebug() << "user[" << i <<"]=" << &m_users[i].name << m_users[i].ip[0] << m_users[i].isAlive;
        if (m_users[i].isAlive) {
            if (netType == 0 && !m_users[i].ip[0].isEmpty()) list.append(m_users[i].name);
            else if (netType == 1 && !m_users[i].ip[1].isEmpty()) list.append(m_users[i].name);
            else if (netType == 2 && !m_users[i].ip[2].isEmpty()) list.append(m_users[i].name);
        }
    }
    return list;
}
void NetworkEngine::parseIncomingSyncData(const QByteArray &data, const QString &senderIpStr) {
#ifndef Q_OS_ANDROID1

    QJsonDocument doc = QJsonDocument::fromJson(data);
//    qDebug() << "@@@ parseIncomingSyncData ndata=" << doc << "-----------------------------------------";
    if (doc.isNull()) return;
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    QString name = obj["name"].toString();
    if (type != "discovery")
    qDebug() << "@@@--------------------------------------- senderIpStr=" << senderIpStr << "type=" << type;

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
        newUser.ip[currentNetType] = senderIpStr;
        m_users.append(newUser);
        peerIndex = m_users.size() - 1;
        needSaveConfig = true; // Появился новый друг — фиксируем в JSON!
    }
    else {
        m_users[peerIndex].isAlive = true;
        for (int i =0; i< 3; i++)
        // Проверяем, изменился ли реальный IP-адрес пира на данном интерфейсе
        if (m_users[peerIndex].ip[i] != senderIpStr) {
            m_users[peerIndex].ip[i] = senderIpStr;
            needSaveConfig = true;
        }
    }

    // Если зафиксировано изменение сетевых адресов — мгновенно обновляем наш единый конфиг на диске
    if (needSaveConfig) {
        savePeersToConfig();
        emit peerListChanged();
    }

    if (type == "incoming_call") {
        qDebug() << "@@@  incoming call 0" << QString(data);
        if (callInfo.busy())
        {
            rejectBusy(senderIpStr);
            return;
        }
        callInfo.netType = obj["net_type"].toInt();
        callInfo.ip = senderIpStr;
        if (callInfo.ip.startsWith("::ffff:"))
            callInfo.ip.remove("::ffff:");

        callInfo.name = name;
        incomingCall();
    }
    else if (type == "accept_call") {
        m_ringbackTone->stop();
        callInfo.setState(CallInfo::speaking);
        audioEngine->startRecording(callInfo.ip);
        emit callAccepted();
    }
    else if (type == "line_busy") {
        m_ringbackTone->stop();
        m_busyTone->play();
        callInfo.setState(CallInfo::idle);
        emit callStopped();
    }
    else if (type == "stop_call") {
        m_ringbackTone->stop();
        m_incomingRing->stop();
        m_busyTone->stop();
        audioEngine->stop();
        callInfo.setState(CallInfo::idle);
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
    callInfo.setState(CallInfo::outCalling);
    if (targetPeerName != "Анфиса")
        sendCallByTcp(targetPeerName, netType);

    QString targetIp = "";
    qDebug() << "startAudioCall()" << "@@@ СЕТЬ C++: Вызов () 1 для:" << targetPeerName;

    for (int i =1; i< m_users.count(); i++) {
        if (m_users[i].name == targetPeerName) {
            targetIp = m_users[i].ip[netType];
            break;
        }
    }
    qDebug() << "@@@ СЕТЬ C++: Вызов startAudioCall() 2 для:" << targetPeerName;

    if (targetIp.isEmpty()) {
        qDebug() << "@@@ СЕТЬ C++ ОШИБКА: Не найден IP-адрес для пользователя:" << targetPeerName;
        emit messageReceived("СИСТЕМА", "Ошибка: IP для " + targetPeerName + " не найден в конфиге.");
        return;
    }

    callInfo.ip = targetIp;
//    m_activeCallNetType = netType;

    QJsonObject obj;
    obj["type"] = "incoming_call";
    obj["name"] = getSavedName();
    obj["net_type"] = QString::number(netType);
    obj["callerIp"] = m_users[0].ip[netType];
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    qDebug() << "@@@ СЕТЬ C++: Вызов startAudioCall() 3 для:" << targetPeerName;

    tcpSocket->abort();
    tcpSocket->connectToHost(targetIp, 28500);
    qDebug() << "@@@ СЕТЬ C++: Вызов startAudioCall() 4 для:" << targetPeerName;

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
    qDebug() << "@@@ СЕТЬ C++: Вызов startAudioCall() 5 для:" << targetPeerName;

    m_ringbackTone->play();
}
void NetworkEngine::acceptAudioCall(const QString &targetPeerName, int netType) {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    m_incomingRing->stop();
    callInfo.setState(CallInfo::speaking);
    QJsonObject obj;
    obj["type"] = "accept_call";
    obj["name"] = me.name;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);


    if (callInfo.ip.startsWith("::ffff:")) {
        callInfo.ip.remove("::ffff:");
    }


    tcpSocket->abort();
    tcpSocket->connectToHost(callInfo.ip, PORT);
    if (tcpSocket->waitForConnected(1200)) {
        tcpSocket->write(data);
        tcpSocket->waitForBytesWritten(500);
    }


    audioEngine->startRecording(callInfo.ip);
}

void NetworkEngine::stopAudioCall() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QJsonObject obj;
    obj["type"] = "stop_call";
    obj["name"] = me.name;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QString targetIp = callInfo.ip;
    if (targetIp.startsWith("::ffff:")) {
        targetIp.remove("::ffff:");
    }
    qDebug() << "@@@Stop Call ip=" << targetIp << "data=" << data;
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
    callInfo.setState(CallInfo::idle);
    callInfo.ip = "";
    callInfo.netType = -1;
    emit callStopped();
}

void NetworkEngine::readConfig() {
    qint64 currMsec = QDateTime::currentMSecsSinceEpoch();
    qint64 dt = currMsec - lastReadConfigTime;
    lastReadConfigTime = currMsec;
    if (m_users.isEmpty()) return;
    QString fName = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf";
    QFile file(fName);
    if (!file.open(QIODevice::ReadOnly)) {
        m_users[0].name = "Пользователь"; // Дефолтное имя, если файла еще нет
        return;
    }

    QByteArray data = file.readAll();
    file.close();
    qDebug() << "@@@ read config " << QString(data);
    if (dt >=0 && dt <=5000)
        return;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        m_users[0].name = "Пользователь";
        return;
    }
    qDebug() << "@@@ read config 2";

    QJsonObject rootObj = doc.object();

    // 1. Восстанавливаем имя владельца рации
    if (rootObj.contains("my_name")) {
        m_users[0].name = rootObj["my_name"].toString().trimmed();
    } else {
        m_users[0].name = "Пользователь";
    }
    qDebug() << "@@@ read config 3";

    // 2. Восстанавливаем кэшированных друзей (Петра, Марью и т.д.)
    if (rootObj.contains("peers") && rootObj["peers"].isArray()) {
        qDebug() << "@@@ read config 3.1";

        QJsonArray peersArray = rootObj["peers"].toArray();
        for (int i = 0; i < peersArray.size(); ++i) {
            QJsonObject peerObj = peersArray.at(i).toObject();
            QString peerName = peerObj["name"].toString();
            qDebug() << "@@@ read config 3.2";

            if (peerName.isEmpty() || peerName == m_users.at(0).name) continue;

            // Проверяем дубликаты на всякий случай
            bool exists = false;
            for (int j = 1; j < m_users.size(); ++j) {
                if (m_users[j].name == peerName) {
                    exists = true;
                    break;
                }
            }
            qDebug() << "@@@ read config 3.3";

            if (!exists) {
                UserInfo savedUser;
                qDebug() << "@@@ read config 3.4";
                savedUser.name = peerName;
//                savedUser.isAlive = false; // Они пока оффлайн, но в списке Петр ПОЯВИТСЯ!
                savedUser.isAlive = peerObj["isAlive"].toBool();
                QJsonArray ipArr = peerObj["ip"].toArray();
                qDebug() << "@@@ read config 3.5";
                for (int i =0; i< 3; i++)
                savedUser.ip[i] = ipArr[i].toString();
                m_users.append(savedUser);
                qDebug() << "@@@ read config 3.6";

            }
        }
    }
    qDebug() << "@@@ read config 4" ;

    if (rootObj.contains("lastCall"))
        {
        QJsonObject callObj = rootObj["lastCall"].toObject();
        qint64 msec = callObj["time"].toInteger();
        qint64 currMsec = QDateTime::currentMSecsSinceEpoch();
        qint64 dt = (currMsec-msec) /1000;
        qDebug() << "@@@Last call" << dt;
        if (dt >= 0 &&  dt< 30)
        {
        pendingCall = true;
        callInfo.name = callObj["name"].toString();
        callInfo.netType = callObj["netType"].toInt();
        QString sip = callObj["ip"].toString();
        if (sip.isEmpty())
        {
            for (int i = 1; i< m_users.count(); i++)
                if (m_users[i].name == callInfo.name)
                    sip = m_users[i].ip[callInfo.netType];
        }
        callInfo.ip =  sip;
        qDebug() << "@@@  lastCall=" << callObj;
        QTimer::singleShot(1000, []() {
            netEngine->incomingCall();
        });
        }
        else {
            QDateTime tfile, tnow;
            tfile.setMSecsSinceEpoch(msec);
            tnow.setMSecsSinceEpoch(msec);
            qDebug() << "tfile,tnow=" << tfile << tnow;

        }
        }
        else
    {
            qDebug() << "@@@  lastCall too old";
        }
        qDebug() << "@@@ read config 5";
        rootObj.remove("lastCall");
        qDebug() << "@@@ read config 6" ;
        QFile writeFile(fName);
        if (writeFile.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(rootObj);
            writeFile.write(doc.toJson(QJsonDocument::Compact));
            writeFile.close();
        }
    else {
        qDebug() << "@@@  lastCall=0";
    }
    emit peerListChanged();
    qDebug() << "@@@ read config exit";

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
        QJsonArray ipArray = peerObj["ip"].toArray();
        peerObj["name"] = m_users[i].name;
        for (int j =0; j<3; j++)
            ipArray.append(m_users[i].ip[j]);
        peerObj["ip"] = ipArray;
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

void NetworkEngine::incomingCall()
{
    qDebug() << "@@@  incoming call 2" ;
    m_incomingRing->play();
    callInfo.setState(CallInfo::inCalling);
    emit setNetType(callInfo.netType);
    emit incomingCall(callInfo.name,callInfo.netType);
}

void NetworkEngine::reject(const QString &ip)
{

}

void NetworkEngine::rejectBusy(const QString &ip)
{
    QJsonObject busyObj;
    busyObj["type"] = "line_busy";
    busyObj["name"] = m_users.at(0).name;
    QByteArray busyData = QJsonDocument(busyObj).toJson(QJsonDocument::Compact);
    if (tcpClientSocket && tcpClientSocket->state() == QAbstractSocket::ConnectedState) {
        tcpClientSocket->write(busyData);
    } else if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
        tcpSocket->write(busyData);
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
        if (!me.ip[0].isEmpty()) {
            udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), PORT);
            udpSent = true;
        }
        if (!me.ip[1].isEmpty()) {
            udpSocket->writeDatagram(data, QHostAddress("192.168.43.255"), PORT);
            udpSocket->writeDatagram(data, QHostAddress("192.168.137.255"), PORT);
            udpSent = true;
        }
        if (!udpSent || !me.ip[2].isEmpty()) {
            for (int i = 1; i < m_users.size(); ++i) {
                if (m_users[i].isAlive && !m_users[i].ip[2].isEmpty()) {
                    QTcpSocket tmpSocket;
                    tmpSocket.connectToHost(m_users[i].ip[2], PORT);
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
            if (!m_users[i].ip[0].isEmpty() && !me.ip[0].isEmpty()) targetIp = m_users[i].ip[0];
            else if (!m_users[i].ip[1].isEmpty() && !me.ip[1].isEmpty()) targetIp = m_users[i].ip[1];
            else if (!m_users[i].ip[2].isEmpty() && !me.ip[2].isEmpty()) targetIp = m_users[i].ip[2];
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
QByteArray NetworkEngine::getCallData(int netType)
{
    UserInfo me = m_users.at(0);
    QJsonObject obj;
    obj["type"] = "incoming_call";
    obj["name"] = me.name;
    obj["netType"] = netType;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    return data;
}

#ifndef Q_OS_ANDROID
void NetworkEngine::sendDiscovery() {
    if (m_users.isEmpty()) return;
    UserInfo me = m_users.at(0);
    QJsonObject obj;
    obj["type"] = "discovery";
    obj["name"] = me.name;
    obj["tail"] = "Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail Tail ";
    QJsonArray ipArr;
    for (int i=0; i< 3; i++)
        ipArr.append(me.ip[i]);
    obj["ip"] = ipArr;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
//    qint64 nb = udpSocket->writeDatagram(data, QHostAddress("192.168.0.255"), PORT);

    qint64 nb = udpSocket->writeDatagram(data, QHostAddress("255.255.255.255"), 28001);
//    qint64 nb = udpSocket->writeDatagram(data, QHostAddress("192.168.43.255"), PORT);
//    qint64 nb = udpSocket->writeDatagram(data, QHostAddress("192.168.137.255"), PORT);
    qDebug() << QTime::currentTime() << "@@@ sendDiscovery" << nb << "bytes";

}
#endif
void NetworkEngine::updateInterfaces() {
    if (m_users.isEmpty()) return;
    m_users[0].ip[0] = "";
    m_users[0].ip[1] = "";
    m_users[0].ip[2] = "";
    QList<QHostAddress> list = QNetworkInterface::allAddresses();
    qDebug() << "@@@ Alladdreses=" << list;
    for (int i = 0; i < list.count(); i++) {
        if (!list[i].isLoopback() && list[i].protocol() == QAbstractSocket::IPv4Protocol) {
            QString ip = list[i].toString();
            qDebug() << "@@@Interface[" << i << "]=" << ip;
            if (ip.startsWith("192.168.49.")) m_users[0].ip[2] = ip;
            else if (ip.startsWith("192.168.43.") ||
                     ip.startsWith("192.168.137.") ||
                     ip.startsWith("10.")) m_users[0].ip[1] = ip;
            else m_users[0].ip[0] = ip;
        }
    }
    m_users[0].name = getSavedName();
}

void NetworkEngine::onNewConnection() {
    qDebug() << "NetworkEngine::onNewConnection()";
    if (tcpClientSocket) {
        tcpClientSocket->disconnectFromHost();
        tcpClientSocket->deleteLater();
    }
    tcpClientSocket = tcpServer->nextPendingConnection();
    connect(tcpClientSocket, &QTcpSocket::readyRead, this, &NetworkEngine::onReadyTcpRead);
}

void NetworkEngine::onReadyTcpRead() {
    qDebug() << "@@@NetworkEngine::onReadyTcpRead() 1";
    QTcpSocket *senderSocket = qobject_cast<QTcpSocket*>(sender());
    if (!senderSocket) return;
    QByteArray data = senderSocket->readAll();
    //QString sdata = data1;
    //QByteArray data(sdata.toUtf8());
    qDebug() << "@@@NetworkEngine::onReadyTcpRead() 2" << QString(data);
    QString ip = senderSocket->peerAddress().toString().toLower();
    if (ip.startsWith("::ffff:"))
        ip.remove("::ffff:");
    parseIncomingSyncData(data, ip);
}

void NetworkEngine::onReadyUdpRead() {
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress senderHost;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &senderHost);
        QString s (datagram);
        if (!s.contains("discovery"))
            qDebug() << "@@@@@@@@@@@@@@@@@@@@@onReadyUdpRead" << s;
        parseIncomingSyncData(datagram, senderHost.toString());
    }
}
#ifdef Q_OS_ANDROID
void NetworkEngine::m_unixStartServer() {
    return;
    if (m_unixServer)
        delete m_unixServer;
    m_unixServer = new QLocalServer(this);

    // Очищаем старые привязки, чтобы файл сокета не блокировался операционной системой
    QLocalServer::removeServer("TeleLocSocketKey");

    if (m_unixServer->listen("TeleLocSocketKey")) {
        qDebug() << "@@@ [NetworkEngine] Unix-сервер УСПЕШНО ЗАПУЩЕН внутри класса!";

        // Привязываем сигнал подключения к слоту нашего класса
        connect(m_unixServer, &QLocalServer::newConnection, this, &NetworkEngine::m_unixOnNewConnection);
    } else {
        qCritical() << "@@@ [NetworkEngine] Ошибка запуска Unix-сервера:" << m_unixServer->errorString();
    }

    // Настраиваем таймер пингов на 10 секунд
    m_unixAliveTimer = new QTimer(this);
    connect(m_unixAliveTimer, &QTimer::timeout, this, &NetworkEngine::m_unixSendAlivePing);
}

void NetworkEngine::m_unixOnNewConnection() {
    // Сохраняем сокет в переменную класса, чтобы он не удалялся из памяти
    m_unixClientSocket = m_unixServer->nextPendingConnection();
    qDebug() << "@@@ [NetworkEngine] Java-служба успешно подключилась по Unix-каналу!";

    // Запускаем таймер и отправляем первый пинг немедленно
    m_unixAliveTimer->start(10000); // 10 секунд
    m_unixSendAlivePing();

    // Слушаем входящие данные (CALL:...) от службы
    connect(m_unixClientSocket, &QLocalSocket::readyRead, this, [this]() {
        if (!m_unixClientSocket) return;

        QByteArray unixData = m_unixClientSocket->readAll();
        QString unixMessage = QString::fromUtf8(unixData);
        qDebug() << "@@@ unixMessage = " << unixMessage;
        debugMsg(unixMessage);
        if (unixMessage.startsWith("CALL:")) {
            QString unixCallerName = unixMessage.mid(5);
            qDebug() << "@@@ [NetworkEngine УСПЕХ!] ПОЛУЧЕН ВХОДЯЩИЙ ЗВОНОК:" << unixCallerName;
            // Здесь ваша логика обработки звонка внутри NetworkEngine
        }
    });

    // Если служба разорвала соединение
    connect(m_unixClientSocket, &QLocalSocket::disconnected, this, [this]() {
        qWarning() << "@@@ [NetworkEngine] Служба отключилась от Unix-сокета. Остановка пингов.";
        m_unixStartServer();
        return;
        if (!m_unixClientSocket) return;

//        m_unixAliveTimer->stop();
//        m_unixClientSocket->deleteLater();
//        m_unixClientSocket = nullptr;
    });
}

void NetworkEngine::m_unixSendAlivePing() {
    if (m_unixClientSocket && m_unixClientSocket->isOpen()) {
        QString alive = firstAlive ? "FIRSTALIVE" : "ALIVE";
        m_unixClientSocket->write(alive.toUtf8());
        firstAlive = false;
        m_unixClientSocket->flush();
//        qDebug() << "@@@ [NetworkEngine] Отправлен Unix-пинг: ALIVE";
    }
}
#endif
void NetworkEngine::debugUsers() {
    updateInterfaces();
    for (int i = 0; i < m_users.size(); ++i) {
        qDebug() << "@@@ User:" << m_users[i].name << "LAN:"
            << m_users[i].ip[0]
            << "AP:" << m_users[i].ip[1] << "Direct:"
            << m_users[i].ip[2] << "Alive:" << m_users[i].isAlive;
    }
}
QList<UserInfo> NetworkEngine::loadPeersFromConfig() {
    QList<UserInfo> activeUsers;
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/teleloc.conf";
    QFile file(configPath);
    qDebug() << "@@@ " << "configpath=" << configPath;
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        qDebug() << "@@@ " << "doc=" << doc;

        file.close();

        if (doc.isObject()) {
            QJsonObject configObj = doc.object();
            QJsonArray peersArray = configObj["peers"].toArray();

            for (const QJsonValue &value : peersArray) {
                QJsonObject peerObj = value.toObject();
                QJsonArray ipArr = peerObj["ip"].toArray();
                qDebug() << "@@@ " << "ipArr=" << ipArr;
                QString name = peerObj["name"].toString();
                qDebug() << "@@@ " << "name=" << name;
                QString ip0 = ipArr[0].toString();
                qDebug() << "@@@ " << "ipArr[0]=" << ip0;

                if (!name.isEmpty() && !ip0.isEmpty()) {
                    UserInfo u;
                    u.name = name;
                    for (int i =0; i< 3; i++)
                    u.ip[i] = ipArr[i].toString();
                    u.isAlive = true;
                    activeUsers.append(u);
                    qDebug() << "@@@ " << "u=" << u.name;
                }
            }
        }
    }
    return activeUsers;
}

void NetworkEngine::debugMsg(const QString &s)
{
    emit messageReceived("Dbg" , s);
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
        QJsonArray ipArr= peerObj["ip"].toArray();
        QString name = peerObj["name"].toString();
        QString ip0 = ipArr[0].toString();

        if (!name.isEmpty() && !ip0.isEmpty()) {
            UserInfo u;
            u.name = name;
            u.ip[0] = ip0;
            u.ip[1] = ipArr[1].toString();
            u.ip[2] = ipArr[2].toString();
            u.isAlive = true;
            m_users.append(u);
        }
    }

    emit usersModelChanged();
}

void CallInfo::setState(State _state)
{
    state = _state;
}
void NetworkEngine::unpressCallButtons()
{
    emit callStopped();
    callInfo.netType = -1;
}
void NetworkEngine::setNetType(int _netType)
{
    callInfo.netType = _netType;
    emit setActiveNetType(_netType);
}


bool NetworkEngine::activeNetType()
{
    return callInfo.netType;
}
