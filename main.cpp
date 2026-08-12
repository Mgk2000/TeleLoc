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
    QString incomingCallerName = "";
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");

    if (context.isValid()) {
        // БЛОК 0: Железобетонный и безопасный запуск Java-службы в Qt 6
        // 1. Получаем объект java.lang.Class для нашей службы через Class.forName()
        QJniObject classNameStr = QJniObject::fromString("org.qtproject.example.appteleloc.TeleLocService");
        QJniObject serviceClass = QJniObject::callStaticObjectMethod(
            "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", classNameStr.object());

        if (serviceClass.isValid()) {
            // 2. Создаем Intent, передавая Context Activity и объект Class
            QJniObject intent("android/content/Intent", "(Landroid/content/Context;Ljava/lang/Class;)V",
                              context.object(), serviceClass.object());

            if (intent.isValid()) {
                // 3. Запускаем Foreground службу в фоне
                context.callObjectMethod("startForegroundService", "(android/content/Intent;)Landroid/content/ComponentName;", intent.object());
            }
        }

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

        // БЛОК 2: Активация флагов вывода окна поверх экрана блокировки
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([context]() {
            QJniObject windowObj = context.callObjectMethod("getWindow", "()Landroid/view/Window;");
            if (windowObj.isValid()) {
                int flags = 0x00080000 | 0x00200000 | 0x00400000;
                windowObj.callMethod<void>("addFlags", "(I)V", flags);
            }
        });

        // БЛОК 3: Достаем интент (вызов из фона)
        QJniObject intentObj = context.callObjectMethod("getIntent", "()Landroid/content/Intent;");
        if (intentObj.isValid()) {
            jboolean hasExtra = intentObj.callMethod<jboolean>("hasExtra", "(Ljava/lang/String;)Z",
                                                               QJniObject::fromString("caller_name").object());
            if (hasExtra) {
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
    // Запускаем службу через 1 секунду после старта приложения
    QTimer::singleShot(1000, []() {
        QJniObject context = QJniObject::callStaticObjectMethod(
            "org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");

        if (context.isValid()) {
            // СТРОГОЕ СООТВЕТСТВИЕ: org.qtproject.example.appTeleLoc (с заглавными T и L!)
            // СТРОГО В НИЖНЕМ РЕГИСТРЕ для рантайма Java в Qt 6!
            QJniObject classNameStr = QJniObject::fromString("org.qtproject.example.appteleloc.TeleLocService");
            QJniObject serviceClass = QJniObject::callStaticObjectMethod(
                "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", classNameStr.object());
            if (serviceClass.isValid()) {
                QJniObject intent("android/content/Intent", "(Landroid/content/Context;Ljava/lang/Class;)V",
                                  context.object(), serviceClass.object());
                if (intent.isValid()) {
                    // ИСПРАВЛЕНО: Строгая JNI-сигнатура со слэшами и точкой с запятой L...;
                    QJniObject componentName = context.callObjectMethod(
                        "startForegroundService",
                        "(Landroid/content/Intent;)Landroid/content/ComponentName;",
                        intent.object()
                        );

                    if (componentName.isValid()) {
                        qDebug() << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ СЛУЖБА УСПЕШНО ЗАПУЩЕНА! ComponentName:" << componentName.toString();
                    } else {
                        qDebug() << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ СБОЙ: Вызов вернул NULL";
                    }
                }
                else {
                                    qDebug() << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@intent not Valid";
                }
            }
            else {
                qDebug() << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@serviceClass.is not Valid";
            }

        }
    });
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
