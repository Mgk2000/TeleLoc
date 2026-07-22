#ifndef NETWORKENGINE_H
#define NETWORKENGINE_H

#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSettings>
#include <QSoundEffect>
#include "audioengine.h"

class NetworkEngine : public QObject
{
    Q_OBJECT

    // Свойство для связывания имени пользователя с QML интерфейсом
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(double micRms READ micRms NOTIFY micRmsChanged)

public:
    explicit NetworkEngine(QObject *parent = nullptr);
    ~NetworkEngine();

    // Системные методы управления сессией
    Q_INVOKABLE void start(const QString &name);
    Q_INVOKABLE void saveNameToFile(const QString &name);
    Q_INVOKABLE QString getSavedName() const;
    Q_INVOKABLE bool isRegistered() const;

    // Геттер и сеттер для свойства username
    QString username() const { return m_username; }
    void setUsername(const QString &name);

    // Работа с чатом и звонками
    Q_INVOKABLE void startChatSession(const QString &targetPeer);
    Q_INVOKABLE void sendChatMessage(const QString &text);
    Q_INVOKABLE void startAudioCall(const QString &targetPeer);
    Q_INVOKABLE void acceptAudioCall();
    Q_INVOKABLE void stopAudioCall();

    double micRms() const { return m_micRms; }

signals:
    void usernameChanged();
    void messageReceived(const QString &sender, const QString &text);
    void incomingCall(const QString &peer);
    void callAccepted();
    void callEnded();
    void micRmsChanged();

private slots:
    void readPendingDatagrams();
    void sendHeartbeat();
    void handleAudioInputReady(const QByteArray &data);

private:
    void processJsonMessage(const QJsonObject &json);
    void sendJsonMessage(const QJsonObject &json);

    // Сетевые ресурсы
    QUdpSocket *m_udpSocket = nullptr;
    quint16 m_port = 45455;
    QTimer *m_heartbeatTimer = nullptr;

    // Идентификация пользователя
    QString m_username;

    // Состояние звонков и чата
    QString m_currentActiveChatPeer;
    QString m_currentActiveCallPeer;
    bool m_isAudioCallActive = false;

    // Движок звука
    AudioEngine m_audioEngine;
    double m_micRms = 0.0;

    // Плеер для зацикленного рингтона входящего вызова
    QSoundEffect *m_ringtonePlayer = nullptr;
};

#endif // NETWORKENGINE_H
