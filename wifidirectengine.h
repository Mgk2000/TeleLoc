#ifndef WIFIDIRECTENGINE_H
#define WIFIDIRECTENGINE_H

#include <QObject>
#include <QStringList>
#include <QMap>

class WifiDirectEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList discoveredDevices READ discoveredDevices NOTIFY discoveredDevicesChanged)

public:
    explicit WifiDirectEngine(QObject *parent = nullptr);
    ~WifiDirectEngine();

    Q_INVOKABLE void startDiscovery();  // Искать дачников в эфире
    Q_INVOKABLE void connectToDevice(const QString &deviceName); // Жесткое сопряжение чипов
    Q_INVOKABLE void stopPeerConnection();

    QStringList discoveredDevices() const { return m_devices; }

signals:
    void discoveredDevicesChanged();
    void connectionSuccess(const QString &info);
    void connectionFailed(const QString &reason);

private:
    QStringList m_devices; // Список имен для QML
    QMap<QString, QString> m_deviceMacs; // Карта связи: Имя -> MAC-адрес
    void initPlatformP2P();
};

#endif // WIFIDIRECTENGINE_H
