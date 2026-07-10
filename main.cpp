#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPermissions> // Подключаем динамические права Qt 6
#include "networkengine.h"
#include "audioengine.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    NetworkEngine netEngine;
    AudioEngine audioEngine;

    // СВЯЗУЮЩИЕ КОНВЕЙЕРЫ ЗВУКА И СЕТИ ТЕЛЕФОНИИ
    QObject::connect(&netEngine, &NetworkEngine::callStarted, &audioEngine, &AudioEngine::startAudio);
    QObject::connect(&netEngine, &NetworkEngine::callEnded, &audioEngine, &AudioEngine::stopAudio);

    QObject::connect(&audioEngine, &AudioEngine::audioReadyToPacket,
                     &netEngine, &NetworkEngine::sendAudioPacket);

    // Звук марширует через безопасную очередь UI-треда
    QObject::connect(&netEngine, &NetworkEngine::sendAudioBlock,
                     &audioEngine, &AudioEngine::handleIncomingAudio, Qt::QueuedConnection);

    // НАМЕРТВО РЕГИСТРИРУЕМ ОБА ДВИЖКА ДЛЯ ИНТЕРФЕЙСА QML
    engine.rootContext()->setContextProperty("_networkEngine", &netEngine);
    engine.rootContext()->setContextProperty("_audioEngine", &audioEngine);

    // ЖЕЛЕЗОБЕТОННЫЙ ЗАПРОС МИКРОФОНА: Заставляем Android выдать права динамически!
    QMicrophonePermission micPermission;
    app.requestPermission(micPermission, [](const QPermission &permission) {
        if (permission.status() == Qt::PermissionStatus::Granted) {
            qDebug() << "=== [АКУСТИКА] УСПЕХ: Доступ к микрофону подтвержден!";
        }
    });

    const QUrl url(QStringLiteral("qrc:/TeleLoc/Main.qml"));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
