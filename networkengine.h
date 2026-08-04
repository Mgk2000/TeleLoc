#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantList>
#include <QHostAddress>
#include <QDateTime>
#include <QList>

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
    Q_PROPERTY(QVariantList activeUsers READ activeUsers NOTIFY activeUsersChanged)
    Q_PROPERTY(QVariantList p2pPeers READ p2pPeers NOTIFY p2pPeersChanged)

public:
    explicit NetworkEngine(QObject *parent = nullptr);
    ~NetworkEngine();

    Q_INVOKABLE void sendMessage(const QString &targetIp, const QString &message);
    Q_INVOKABLE void startAudioCall(const QString &targetPeerName);
    Q_INVOKABLE void startWifiDirectScan();
    Q_INVOKABLE void connectToWifiDirectDevice(const QString &macAddress);
    Q_INVOKABLE void tryConnectToMaster();
    Q_INVOKABLE bool isRegistered() const;
    Q_INVOKABLE QString getSavedName() const;
    Q_INVOKABLE void createAndroidP2pGroup();
    Q_INVOKABLE void debugUsers() const;
    Q_INVOKABLE QVariantList getUsers(int netType) const;

    QVariantList activeUsers() const;
    QVariantList p2pPeers() const;

signals:
    void activeUsersChanged();
    void p2pPeersChanged();
    void messageReceived(QString fromIp, QString message);

private slots:
    void sendDiscovery();
    void readPendingDatagrams();
    void onNewConnection();
    void onReadyTcpRead();

private:
    void readConfig();
    void updateInterfaces();
    void sendUsersDataTcp(QTcpSocket *socket);
    QUdpSocket *udpSocket;
    QTcpServer *tcpServer;
    QTcpSocket *tcpSocket;
    QTcpSocket *tcpClientSocket;
    QTimer *discoveryTimer;
    QTimer *interfaceTimer;
    QList<UserInfo> m_users;
    QVariantList m_p2pPeersList;
    const QString SERVER_IP = "192.168.49.1";
    const quint16 PORT = 45455;
        void parseIncomingSyncData(const QByteArray &data, const QString &senderIpStr);
};

#endif
