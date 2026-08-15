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
#endif

int main(int argc, char *argv[])
{
#ifdef WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    QLocale::setDefault(QLocale::c());
#endif

    QGuiApplication app(argc, argv);
#ifdef Q_OS_ANDROID
#include <QtCore/private/qandroidextras_p.h>

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
         //   qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Таймер сработал, ищу класс службы...";

            // Вариант 1 (маленькие буквы)
            QJniObject classNameStr = QJniObject::fromString("org.qtproject.example.appteleloc.TeleLocService");
            QJniObject serviceClass = QJniObject::callStaticObjectMethod(
                "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", classNameStr.object());

            // Вариант 2 (заглавные буквы)
            if (!serviceClass.isValid()) {
                //qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Строчный вариант мимо, пробую appTeleLoc...";
                classNameStr = QJniObject::fromString("org.qtproject.example.appTeleLoc.TeleLocService");
                serviceClass = QJniObject::callStaticObjectMethod(
                    "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", classNameStr.object());
            }

            if (serviceClass.isValid()) {
               // qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: КЛАСС СЛУЖБЫ НАЙДЕН! Стреляю startForegroundService...";
                QJniObject intent("android/content/Intent", "(Landroid/content/Context;Ljava/lang/Class;)V",
                                  context.object(), serviceClass.object());
                if (intent.isValid()) {
                    QJniObject componentName = context.callObjectMethod(
                        "startForegroundService",
                        "(Landroid/content/Intent;)Landroid/content/ComponentName;",
                        intent.object()
                        );
                    //qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Результат запуска компонент =" << (componentName.isValid() ? "УСПЕХ" : "NULL");
                }
            } else {
               // qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: КАТАСТРОФА! Класс Java вообще не обнаружен в DEX.";
            }
        } else {
            qDebug() << "@@@@@@@@@@ C++ ПРОВЕРКА: Контекст Activity невалиден!";
        }
    });

    // БЛОК 5: Обработка только холодного старта рации
    if (!incomingCallerName.isEmpty()) {
        QTimer::singleShot(500, &netEngine, [&netEngine, incomingCallerName]() {
            netEngine.handleVoipWakeup(incomingCallerName);
        });
    }
#endif

    engine.rootContext()->setContextProperty("netEngine", &netEngine);

    const QUrl url(QStringLiteral("qrc:/qt/qml/TeleLoc/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
