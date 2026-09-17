#ifdef WIN32
#include <windows.h>
#endif
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>
#include <QTimer>
#include "networkengine.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/qnativeinterface.h>
#include <QtCore/private/qandroidextras_p.h>
#include <QLoggingCategory>
#include <android/log.h>

#endif
#include <QGuiApplication>
#include <QQmlApplicationEngine>

void applyOrientationBasedOnDeviceType() {
#ifdef Q_OS_ANDROID
    // 1. Получаем объект текущей Activity
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;"
        );

    if (!activity.isValid()) return;

    // 2. Получаем конфигурацию устройства
    QJniObject resources = activity.callObjectMethod("getResources", "()Landroid/content/res/Resources;");
    QJniObject configuration = resources.callObjectMethod("getConfiguration", "()Landroid/content/res/Configuration;");

    // Получаем поле smallestScreenWidthDp (минимальная ширина экрана в dp)
    jint smallestScreenWidthDp = configuration.getField<jint>("smallestScreenWidthDp");

    // Google Standard: sw600dp и выше — это планшеты (7 дюймов и более)
    bool isTablet = (smallestScreenWidthDp >= 600);

    // 3. Задаем ориентацию:
    // 0 = SCREEN_ORIENTATION_LANDSCAPE (альбомная)
    // 1 = SCREEN_ORIENTATION_PORTRAIT (книжная)
    jint requestedOrientation = isTablet ? 0 : 1;

    // Принудительно устанавливаем ориентацию
    activity.callMethod<void>("setRequestedOrientation", "(I)V", requestedOrientation);
#endif
}
int main(int argc, char *argv[])
{
#ifdef WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    QLocale::setDefault(QLocale::c());
#endif
    QGuiApplication app(argc, argv);
#ifdef Q_OS_ANDROID
    qDebug() << "@@@ TeleLoc started";
    applyOrientationBasedOnDeviceType();

    // ФОРСИРОВАННЫЙ ЗАПРОС ПРАВ ДЛЯ QT 6.8.3 (Официальная сигнатура из одной строки)
    QStringList permissions = {
        "android.permission.RECORD_AUDIO",
        "android.permission.POST_NOTIFICATIONS",
        "android.permission.FOREGROUND_SERVICE"
    };

    for (const QString &permission : permissions) {
        // Вызываем строго с одним параметром, как требует заголовочный файл Qt
        QtAndroidPrivate::requestPermission(permission);
    }
#endif

    QString incomingCallerName = "";

#ifdef Q_OS_ANDROID
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");

    if (context.isValid()) {
        // БЛОК 1: Настройка WakeLock (Процессор не засыпает)
        QJniObject serviceName = QJniObject::getStaticObjectField(
            "android/content/Context", "POWER_SERVICE", "Ljava/lang/String;");
        QJniObject powerManager = context.callObjectMethod(
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", serviceName.object());
        if (powerManager.isValid()) {
            QJniObject wakeLock = powerManager.callObjectMethod(
                "newWakeLock", "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
                0x00000001, QJniObject::fromString("TeleLoc::WakeLock").object());
            if (wakeLock.isValid()) {
                wakeLock.callMethod<void>("acquire");
            }
        }

        // БЛОК 2: Активация флагов вывода окна поверх экрана блокировки на UI-потоке Android
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([context]() {
            QJniObject windowObj = context.callObjectMethod("getWindow", "()Landroid/view/Window;");
            if (windowObj.isValid()) {
                int flags = 0x00080000 | 0x00200000 | 0x00400000;
                windowObj.callMethod<void>("addFlags", "(I)V", flags);
            }
        });

        // БЛОК 3: Достаем интент и вытаскиваем имя звонящего (Холодный старт)
        QJniObject intentObj = context.callObjectMethod("getIntent", "()Landroid/content/Intent;");
        if (intentObj.isValid()) {
            jboolean hasName = intentObj.callMethod<jboolean>("hasExtra", "(Ljava/lang/String;)Z",
                                                              QJniObject::fromString("caller_name").object());
            if (hasName) {
                QJniObject jCallerName = intentObj.callObjectMethod("getStringExtra",
                                                                    "(Ljava/lang/String;)Ljava/lang/String;",
                                                                    QJniObject::fromString("caller_name").object());
                if (jCallerName.isValid()) {
                    incomingCallerName = jCallerName.toString();
                }
            }
        }
    }
#endif

    QQmlApplicationEngine engine;
    NetworkEngine netEngine;
#ifdef Q_OS_ANDROID
    // БЛОК 4: НЕЗАВИСИМЫЙ ДИНАМИЧЕСКИЙ ЗАПУСК ФОНОВОЙ СЛУЖБЫ НА ПОРТУ 28500
    QTimer::singleShot(1000, []() {
        QJniObject context = QJniObject::callStaticObjectMethod(
            "org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");

        if (context.isValid()) {
            qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Таймер сработал, ищу класс службы...";

            // Вариант 1 (маленькие буквы)
            QJniObject classNameStr = QJniObject::fromString("org.qtproject.example.appTeleLoc.TeleLocService");
            QJniObject serviceClass = QJniObject::callStaticObjectMethod(
                "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", classNameStr.object());

            // Вариант 2 (заглавные буквы)
            if (!serviceClass.isValid()) {
                qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Строчный вариант мимо, пробую appTeleLoc...";
                classNameStr = QJniObject::fromString("org.qtproject.example.appTeleLoc.TeleLocService");
                serviceClass = QJniObject::callStaticObjectMethod(
                    "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", classNameStr.object());
            }

            if (serviceClass.isValid()) {
                qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: КЛАСС СЛУЖБЫ НАЙДЕН! Стреляю startForegroundService...";
                QJniObject intent("android/content/Intent", "(Landroid/content/Context;Ljava/lang/Class;)V",
                                  context.object(), serviceClass.object());
                if (intent.isValid()) {
                    QJniObject componentName = context.callObjectMethod(
                        "startForegroundService",
                        "(Landroid/content/Intent;)Landroid/content/ComponentName;",
                        intent.object()
                        );
                    qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Результат запуска компонент =" << (componentName.isValid() ? "УСПЕХ" : "NULL");
                }
            } else {
                qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: КАТАСТРОФА! Класс Java вообще не обнаружен в DEX.";
            }
        } else {
            qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Контекст Activity невалиден!";
        }
    });
    // ИСПРАВЛЕНО ДЛЯ БАГА №1: Проверяем и запрашиваем разрешение на показ окон из фона
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");
    if (activity.isValid()) {
        QJniObject context = activity.callObjectMethod("getApplicationContext", "()Landroid/content/Context;");
        jboolean canDraw = QJniObject::callStaticMethod<jboolean>(
            "android/provider/Settings", "canDrawOverlays", "(Landroid/content/Context;)Z", context.object());

        if (!canDraw) {
            qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Нет прав рисовать поверх окон! Отправляю пользователя в настройки...";
            QJniObject intent("android/content/Intent", "()V");
            QJniObject action = QJniObject::getStaticObjectField("android/provider/Settings", "ACTION_MANAGE_OVERLAY_PERMISSION", "Ljava/lang/String;");

            QString pkgName = context.callObjectMethod<jstring>("getPackageName").toString();
            QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                                                QJniObject::fromString("package:" + pkgName).object());

            intent.callObjectMethod("setAction", "(Ljava/lang/String;)Landroid/content/Intent;", action.object());
            intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", uri.object());
            intent.callMethod<void>("addFlags", "(I)V", 0x10000000);

            activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
        }
    }

    // БЛОК 5: Обработка только холодного старта рации
    if (!incomingCallerName.isEmpty()) {
        QTimer::singleShot(500, &netEngine, [&netEngine, incomingCallerName]() {
            netEngine.handleVoipWakeup(incomingCallerName);
        });
    }
#endif

    engine.rootContext()->setContextProperty("netEngine", &netEngine);
    engine.rootContext()->setContextProperty("AudioEngine", netEngine.audioEngine);
    engine.rootContext()->setContextProperty("myUsersModel", netEngine.usersModel);

    const QUrl url(QStringLiteral("qrc:/qt/qml/TeleLoc/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
