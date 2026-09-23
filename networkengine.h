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
#include <QAbstractListModel>
#include <QDateTime>
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
class UsersModel;
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
    Q_INVOKABLE bool isCallIdle () const {return callInfo.state == CallInfo::idle;}
    Q_INVOKABLE bool isCallCalling () const
        {return callInfo.state == CallInfo::inCalling ||
                 callInfo.state == CallInfo::outCalling;}
    Q_INVOKABLE bool isCallSpeaking () const {return callInfo.state == CallInfo::speaking;}
    CallInfo callInfo;
    AudioEngine *audioEngine;
    UsersModel* usersModel;

    QByteArray getCallData(int netType);
    Q_INVOKABLE void masterStartRecording();
    Q_INVOKABLE void masterStopRecording();

    void sendTCP(const QString &command);
    void configFromString(const QString & sconf);
    void setUsersModel(UsersModel* _model)
        {usersModel = _model;}
    void incomingCall();
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
    void setCallState(int st);

private slots:
    void onNewConnection();
    void onReadyTcpRead();
    void onReadyUdpRead();
#ifndef Q_OS_ANDROID
    void sendDiscovery();
    void updateInterfaces();
#else
    void m_unixOnNewConnection();
    void m_unixSendAlivePing();

#endif
private:
    void readConfig(bool checkLastCall);
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
    void reject(const QString & ip);
    void rejectBusy(const QString & ip);
    qint64 lastReadConfigTime = 0;
    void setCallingState(CallInfo::State);

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
    void updateUsersList();
    void deleteLastCall();
};

class UsersModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum UserRoles {
        NameRole = Qt::UserRole + 1,
        IpLocalRole,
        IpSpotRole,
        IpDirectRole,
        IsAliveRole
    };

    explicit UsersModel(QObject *parent) : QAbstractListModel(parent)
    {
        netEngine = (NetworkEngine*) parent;
    }

    // Метод для заполнения модели вашими данными
    void setUsers(const QList<UserInfo> &users) {
        for (int i =0; i< users.count(); i++)
            qDebug() << "@@@setUsers " << i << users[i].name;
        beginResetModel();
        m_users = users.mid(1, 1000);
        endResetModel();
    }

    // Обязательные методы для переопределения
    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return m_users.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_users.size()) return QVariant();

        const auto &user = m_users[index.row()];

        switch (role) {
        case NameRole:     return user.name;
        case IpLocalRole:  return user.ip[0];
        case IpSpotRole:   return user.ip[1];
        case IpDirectRole: return user.ip[2];
        case IsAliveRole:  return user.isAlive();
        default:           return QVariant();
        }
    }
    void updateAllUsers(const QList<UserInfo>& newUsers) {
        // 1. Сообщаем QML, что модель начинает полную перезагрузку
        beginResetModel();

        // 2. Меняем данные внутри вашей модели
        m_users = newUsers;

        // 3. Сообщаем QML, что обновление завершено.
        // В этот же миг ListView в QML полностью и мгновенно перерисуется!
        endResetModel();
    }

protected:
    QHash<int, QByteArray> roleNames() const override {
        QHash<int, QByteArray> roles;
        roles[NameRole]     = "userName";
        roles[IpLocalRole]  = "ipLocal";
        roles[IpSpotRole]   = "ipSpot";
        roles[IpDirectRole] = "ipDirect";
        roles[IsAliveRole]  = "isAlive";
        return roles;
    }

private:
    QList<UserInfo> m_users;
    NetworkEngine * netEngine;

};

#endif // NETWORKENGINE_H
