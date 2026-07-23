#ifdef WIN32
#include <windows.h>
#endif
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "networkengine.h"

int main(int argc, char *argv[])
{
#ifdef WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    // Создаем единственный корневой сетевой движок
    NetworkEngine netEngine;

    // Регистрируем его в QML под именем netEngine для доступа из интерфейса
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
