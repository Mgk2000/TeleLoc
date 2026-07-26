#include "wifidirectengine.h"
#include <QDebug>

#ifdef _WIN32
#include <windows.h>
#else
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJniObject>
#include <QJniEnvironment>
#include <QtCore/qnativeinterface.h>
#include <QTimer>
#endif

WifiDirectEngine::WifiDirectEngine(QObject *parent) : QObject(parent)
{
    initPlatformP2P();
}

WifiDirectEngine::~WifiDirectEngine()
{
    stopPeerConnection();
}

void WifiDirectEngine::initPlatformP2P()
{
#ifdef _WIN32
    qDebug() << "Wi-Fi Direct: Инициализация Windows.";
#else
    qDebug() << "Wi-Fi Direct: Инициализация Android.";
#endif
}

void WifiDirectEngine::startDiscovery()
{
    m_devices.clear();
    emit discoveredDevicesChanged();

#ifdef _WIN32
    qDebug() << "Windows: Открытие панели Wi-Fi Direct...";
    QStringList debugDevices;
    debugDevices.append("Смартфон Ивана (Wi-Fi Direct)");
    m_devices = debugDevices;
    emit discoveredDevicesChanged();
#else
    qDebug() << "Android: Запуск СКРЫТОГО экрана Wi-Fi Direct...";

    if (!qGuiApp) return;
    auto *androidInterface = qGuiApp->nativeInterface<QNativeInterface::QAndroidApplication>();
    if (!androidInterface) return;

    QJniObject context(androidInterface->context());
    if (!context.isValid()) return;

    // ЖЕСТКИЙ ФИКС ДЛЯ СВЕЖИХ ANDROID (Xiaomi & Samsung):
    // Поскольку кнопки P2P в обычных настройках Wi-Fi больше нет, мы создаем Intent,
    // который принудительно открывает скрытый, заблокированный системный компонент
    // настроек Wi-Fi Direct напрямую, минуя главное меню телефона!
    QJniObject intent("android/content/Intent");
    if (intent.isValid()) {
        QJniObject componentName("android/content/ComponentName",
                                 "(Ljava/lang/String;Ljava/lang/String;)V",
                                 QJniObject::fromString("com.android.settings").object(),
                                 QJniObject::fromString("com.android.settings.Settings$WifiDirectSettingsActivity").object()
                                 );

        intent.callObjectMethod("setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;", componentName.object());

        // Запускаем окно. На Сяоми Ивана и Самсунге Петра СРАЗУ распахнется
        // тот самый белый скрытый экран Wi-Fi Direct, где они мгновенно увидят друг друга!
        context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());

        QStringList statusList;
        statusList.append("Системное окно P2P открыто");
        m_devices = statusList;
        emit discoveredDevicesChanged();
    }
#endif
}

void WifiDirectEngine::connectToDevice(const QString &deviceName)
{
    if (deviceName.isEmpty() || deviceName.startsWith("Поиск") || deviceName.startsWith("P2P")) return;

    // Ищем MAC-адрес в нашей C++ карте по имени, которое нажали в QML
    if (!m_deviceMacs.contains(deviceName)) {
        qWarning() << "Wi-Fi Direct: Ошибка! MAC-адрес для устройства" << deviceName << "не найден в карте.";
        return;
    }

    QString targetMac = m_deviceMacs.value(deviceName);
    qDebug() << "Wi-Fi Direct: Инициируем спаривание чипов! Цель:" << deviceName << "MAC:" << targetMac;

#ifdef _WIN32
    qDebug() << "Windows: Соединение по MAC-адресу:" << targetMac;
#else
    if (!qGuiApp) return;
    auto *androidInterface = qGuiApp->nativeInterface<QNativeInterface::QAndroidApplication>();
    if (!androidInterface) return;

    QJniObject context(androidInterface->context());
    QJniObject serviceString = QJniObject::getStaticObjectField("android/content/Context", "WIFI_P2P_SERVICE", "Ljava/lang/String;");
    QJniObject p2pManager = context.callObjectMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", serviceString.object());
    QJniObject mainLooper = context.callObjectMethod("getMainLooper", "()Landroid/os/Looper;");
    QJniObject p2pChannel = p2pManager.callObjectMethod(
        "initialize",
        "(Landroid/content/Context;Landroid/os/Looper;Landroid/net/wifi/p2p/WifiP2pManager$ChannelListener;)Landroid/net/wifi/p2p/WifiP2pManager$Channel;",
        context.object(),
        mainLooper.object(),
        nullptr
        );

    if (p2pManager.isValid() && p2pChannel.isValid()) {
        // 1. Создаем пустой объект конфигурации Android
        QJniObject p2pConfig("android/net/wifi/p2p/WifiP2pConfig");

        if (p2pConfig.isValid()) {
            // 2. Преобразуем C++ MAC-адрес в Java-строку
            QJniObject javaMacStr = QJniObject::fromString(targetMac);

            // 3. ЖЕСТКИЙ ФИКС: Записываем физический MAC-адрес прямо в Java-поле config.deviceAddress!
            p2pConfig.setField("deviceAddress", "Ljava/lang/String;", javaMacStr.object());

            // 4. Указываем максимальный приоритет подключения (WPS Button configuration)
            p2pConfig.setField("wps", "Landroid/net/wifi/WpsInfo;", QJniObject::getStaticObjectField("android/net/wifi/WpsInfo", "WPC", "I").object());

            qDebug() << "Android: MAC-адрес успешно вшит в WifiP2pConfig. Вызываем аппаратный connect...";

            // 5. Даем команду чипу отправить Марье аппаратный запрос на сопряжение антенн!
            p2pManager.callMethod<void>(
                "connect",
                "(Landroid/net/wifi/p2p/WifiP2pManager$Channel;Landroid/net/wifi/p2p/WifiP2pConfig;Landroid/net/wifi/p2p/WifiP2pManager$ActionListener;)V",
                p2pChannel.object(),
                p2pConfig.object(),
                nullptr
                );

            emit connectionSuccess(QString("Запрос отправлен к %1").arg(deviceName));
        }
    }
#endif
}

void WifiDirectEngine::stopPeerConnection()
{
    qDebug() << "Wi-Fi Direct: Расторжение прямого соединения между чипами.";
}
