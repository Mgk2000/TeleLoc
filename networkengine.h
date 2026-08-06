#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>
#include <QHostAddress>
#include <QList>
#include <QVariantList>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStringList>
#include "audioengine.h"

struct UserInfo {
    QString name;
    QString ip0;
    QString ip1;
    QString ip2;
    QDateTime lastSeen;
    bool isAlive;
};

class NetworkEngine : public QObject
{
    Q_OBJECT

public:
    explicit NetworkEngine(QObject *parent = nullptr);
    ~NetworkEngine();

    Q_INVOKABLE QVariantList getUsers(int netType) const;
    Q_INVOKABLE bool isNetTypeAvailable(int netType) const;

    Q_INVOKABLE void sendMessage(const QString &targetPeer, const QString &text);
    Q_INVOKABLE void startAudioCall(const QString &targetPeerName, int netType);
    Q_INVOKABLE void acceptAudioCall(const QString &targetPeerName, int netType);
    Q_INVOKABLE void stopAudioCall();

    Q_INVOKABLE QString getSavedName() const;
    Q_INVOKABLE void saveNameToFile(const QString &name);
    Q_INVOKABLE bool isRegistered() const { return true; }
    Q_INVOKABLE void debugUsers() const;

signals:
    void peerListChanged();
    void messageReceived(QString fromIp, QString text);
    void incomingCall(QString peerName, int netType);
    void callAccepted();
    void callStopped();

private slots:
    void sendDiscovery();
    void readPendingDatagrams();
    void onNewConnection();
    void onReadyTcpRead();

private:
    void readConfig();
    void updateInterfaces();
    void parseIncomingSyncData(const QByteArray &data, const QString &senderIpStr);

    QUdpSocket *udpSocket;
    QTcpServer *tcpServer;
    QTcpSocket *tcpSocket;
    QTcpSocket *tcpClientSocket;

    QTimer *discoveryTimer;
    QTimer *interfaceTimer;
    AudioEngine *audioEngine;

    QList<UserInfo> m_users;
    QString m_activePeerIp;
    int m_activeCallNetType;

    const quint16 PORT = 45455;
    const QString SERVER_IP = "192.168.49.1";
};

#endif
