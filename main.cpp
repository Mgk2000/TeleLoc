#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "networkengine.h"
#include "audioengine.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    NetworkEngine netEngine;
    AudioEngine audioEngine;

    netEngine.setAudioEngine(&audioEngine);

    // Управление питанием аудиоплаты АТС
    QObject::connect(&netEngine, &NetworkEngine::callStarted, &audioEngine, &AudioEngine::start);
    QObject::connect(&netEngine, &NetworkEngine::callEnded, &audioEngine, &AudioEngine::stop);

    // Микрофон -> Сеть
    QObject::connect(&audioEngine, &AudioEngine::audioDataReady,
                     &netEngine, &NetworkEngine::sendAudioPacket);

    // Сеть -> Динамик (ИСПРАВЛЕНО: Теперь sendAudioBlock легитимен!)
    QObject::connect(&netEngine, &NetworkEngine::sendAudioBlock,
                     &audioEngine, &AudioEngine::playAudioBlock, Qt::QueuedConnection);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("_networkEngine", &netEngine);
    engine.rootContext()->setContextProperty("_audioEngine", &audioEngine);

    const QUrl url(u"qrc:/TeleLoc/Main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
