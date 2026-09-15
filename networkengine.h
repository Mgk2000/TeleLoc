#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QTcpSocket>
#include <QTcpServer>
#include <QUdpSocket>
#include <QSoundEffect>
#include <QUrl>
#include <QTimer>
#include "audioengine.h"
class QLocalServer;
class QLocalSocket;
#define NEWUSER "НЕКТО"
struct UserInfo {
    QString name;
    QString ip[3];
    qint64 lastPing;
    bool isAlive () const;
};
struct CallInfo {
    enum State {idle, inCalling, outCalling, speaking};
    QString name, ip, type;
    int netType;
    qint64 time;
    State state = idle;
    void setState(State _state);
    bool busy() const {
        return state != idle;
    }
};

class NetworkEngine : public QObject
{
    Q_OBJECT
public:
    explicit NetworkEngine(QObject *parent = nullptr);
    ~NetworkEngine();

    Q_INVOKABLE bool isNetTypeAvailable(int netType);
    Q_INVOKABLE void startAudioCall(const QString &targetPeerName, int netType);
    Q_INVOKABLE void acceptAudioCall(const QString &targetPeerName, int netType);
    Q_INVOKABLE void stopAudioCall();
    Q_INVOKABLE void sendMessage(const QString &targetPeer, const QString &text);
    Q_INVOKABLE QString getSavedName();
    Q_INVOKABLE void saveName(const QString &name);
    Q_INVOKABLE QStringList getUsers(int netType);
    Q_INVOKABLE void debugUsers();
    void handleVoipWakeup(const QString &callerName);
    void parseIncomingSyncData(const QByteArray &data, const QString &senderIpStr);
    //QList<UserInfo> loadPeersFromConfig();
    void debugMsg(const QString& s);
    void unpressCallButtons();
    void pressCallButon(int _netType);
    Q_INVOKABLE bool activeNetType();
    CallInfo callInfo;
    AudioEngine *audioEngine;

    QByteArray getCallData(int netType);
    Q_INVOKABLE void masterStartRecording();
    Q_INVOKABLE void masterStopRecording();

    void sendTCP(const QString &command);
    void configFromString(const QString & sconf);

signals:
    void peerListChanged();
    void messageReceived(const QString &fromIp, const QString &message);
    void incomingCall(const QString &peerName, int netType);
    void callAccepted();
    void callStopped();
    void callPressed(int _netType);
    void micVolumeUpdated(int volume);
    void netVolumeUpdated(int volume);
    void usersModelChanged();
    void setActiveNetType(int _netType);

private slots:
    void onNewConnection();
    void onReadyTcpRead();
    void onReadyUdpRead();
#ifndef Q_OS_ANDROID
    void sendDiscovery();
#else
    void m_unixOnNewConnection();
    void m_unixSendAlivePing();

#endif
private:
    void readConfig();
    void savePeersToConfig();
    QTcpServer *tcpServer;
    QTcpSocket *tcpSocket;
    QTcpSocket *tcpClientSocket;
    QUdpSocket *udpSocket;

    QList<UserInfo> m_users;
    const int PORT = 28000;

    QSoundEffect *m_ringbackTone;
    QSoundEffect *m_busyTone;
    QSoundEffect *m_incomingRing;
    bool pendingCall = false;
    void incomingCall();
    void reject(const QString & ip);
    void rejectBusy(const QString & ip);
    qint64 lastReadConfigTime = 0;
#ifdef Q_OS_ANDROID
    QLocalServer* m_unixServer = 0;
    QLocalSocket* m_unixClientSocket;
    void m_unixStartServer();
    void unixSendAlivePing();
    bool firstAlive = true;
    void sendCommandToTeleLocService(int commandId, const QString &payload);
#else
    void processDiscovery(const QString & name, const QJsonObject & obj);
    void saveConfig();
#endif
    QTimer* m_unixAliveTimer;

    void sendCall(const QString&, int);
    QString myIp(int netType) const {
        if (m_users.isEmpty() || m_users[0].name.isEmpty())
            return "";
        else
            return m_users[0].ip[netType];
    }
    void sendCallByTcp(const QString &name, int netType);
    void setNetType(int _netType);

    void deleteLastCall();
};

#endif // NETWORKENGINE_H
