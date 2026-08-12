#include "audioengine.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QHostAddress>
#include <QDebug>

// Глобальный Jitter-буфер для сглаживания входящего сетевого потока
static QByteArray m_ringBuffer;

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent), m_audioSource(nullptr), m_audioSink(nullptr),
    m_inputDevice(nullptr), m_outputDevice(nullptr), m_tcpAudioSocket(nullptr), m_tcpAudioClient(nullptr)
{
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice defaultInput = QMediaDevices::defaultAudioInput();
    QAudioDevice defaultOutput = QMediaDevices::defaultAudioOutput();

    m_audioSource = new QAudioSource(defaultInput, m_format, this);
    m_audioSink = new QAudioSink(defaultOutput, m_format, this);

    m_tcpAudioServer = new QTcpServer(this);
    connect(m_tcpAudioServer, &QTcpServer::newConnection, this, &AudioEngine::onNewConnection);
    m_tcpAudioServer->listen(QHostAddress::Any, AUDIO_PORT);

    m_tcpAudioSocket = new QTcpSocket(this);
    // ОБЯЗАТЕЛЬНО привязываем приём звука к исходящему сокету Ивана!
    connect(m_tcpAudioSocket, &QTcpSocket::readyRead, this, &AudioEngine::onReadyRead);
}

AudioEngine::~AudioEngine()
{
    stop();
}

void AudioEngine::onNewConnection()
{
    if (m_tcpAudioClient) {
        m_tcpAudioClient->disconnectFromHost();
        m_tcpAudioClient->deleteLater();
    }
    m_tcpAudioClient = m_tcpAudioServer->nextPendingConnection();
    if (m_tcpAudioClient) {
        // Привязываем приём звука к входящему сокету Анфисы
        connect(m_tcpAudioClient, &QTcpSocket::readyRead, this, &AudioEngine::onReadyRead);

        if (m_audioSink) {
            m_ringBuffer.clear();
            m_outputDevice = m_audioSink->start();
            m_audioSink->setBufferSize(6400);
        }

        if (m_audioSource && !m_inputDevice) {
            m_inputDevice = m_audioSource->start();
            if (m_inputDevice) {
                connect(m_inputDevice, &QIODevice::readyRead, this, [this]() {
                    if (!m_inputDevice) return;
                    QByteArray rawData = m_inputDevice->readAll();
                    if (rawData.isEmpty()) return;

                    int samplesCount = rawData.size() / 2;
                    int16_t *samples = reinterpret_cast<int16_t*>(rawData.data());
                    int32_t maxVal = 0;
                    for (int i = 0; i < samplesCount; ++i) {
                        samples[i] = static_cast<int16_t>(samples[i] / 2.5);
                        if (qAbs(samples[i]) > maxVal) {
                            maxVal = qAbs(samples[i]);
                        }
                    }

                    int currentVolume = static_cast<int>((maxVal / 13107.0) * 100);
                    if (currentVolume > 100) currentVolume = 100;
                    emit micVolumeChanged(currentVolume);

                    if (m_tcpAudioClient && m_tcpAudioClient->state() == QAbstractSocket::ConnectedState) {
                        m_tcpAudioClient->write(rawData);
                    }
                });
            }
        }
    }
}

void AudioEngine::startRecording(const QString &targetIp)
{
    if (!m_audioSource || !m_audioSink) return;

    stop();

    // Если IP пустой, значит мы Анфиса — мы просто включили сервер в onNewConnection и ждем коннекта Ивана
    if (targetIp.isEmpty()) {
        return;
    }

    // Если IP передан, значит мы Иван — мы инициируем аудио-подключение к Анфисе
    if (m_tcpAudioSocket->state() != QAbstractSocket::ConnectedState) {
        m_tcpAudioSocket->abort();
        m_tcpAudioSocket->connectToHost(targetIp, AUDIO_PORT);
        if (m_tcpAudioSocket->waitForConnected(1200)) {
            m_ringBuffer.clear();
            m_outputDevice = m_audioSink->start();
            m_audioSink->setBufferSize(6400);

            m_inputDevice = m_audioSource->start();
            if (m_inputDevice) {
                connect(m_inputDevice, &QIODevice::readyRead, this, [this]() {
                    if (!m_inputDevice) return;
                    QByteArray rawData = m_inputDevice->readAll();
                    if (rawData.isEmpty()) return;

                    int samplesCount = rawData.size() / 2;
                    int16_t *samples = reinterpret_cast<int16_t*>(rawData.data());
                    int32_t maxVal = 0;
                    for (int i = 0; i < samplesCount; ++i) {
                        samples[i] = static_cast<int16_t>(samples[i] / 2.5);
                        if (qAbs(samples[i]) > maxVal) {
                            maxVal = qAbs(samples[i]);
                        }
                    }

                    int currentVolume = static_cast<int>((maxVal / 13107.0) * 100);
                    if (currentVolume > 100) currentVolume = 100;
                    emit micVolumeChanged(currentVolume);

                    if (m_tcpAudioSocket && m_tcpAudioSocket->state() == QAbstractSocket::ConnectedState) {
                        m_tcpAudioSocket->write(rawData);
                    }
                });
            }
        }
    }
}

void AudioEngine::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || !m_outputDevice || !m_outputDevice->isOpen() || !m_outputDevice->isWritable()) return;

    QByteArray incomingData = socket->readAll();
    m_ringBuffer.append(incomingData);

    // Статический счетчик, чтобы не спамить в лог непрерывно
    static int packetCounter = 0;
    packetCounter++;
    if (packetCounter % 50 == 0) {
        qDebug() << "=== АУДИОДВИЖОК: Приняли из сети порцию звука куском:" << incomingData.size() << "байт. В Jitter-буфере сейчас:" << m_ringBuffer.size() << "байт.";
    }

    if (m_ringBuffer.size() > 19200) {
        m_ringBuffer.remove(0, m_ringBuffer.size() - 3200);
    }
    // Нарезаем и воспроизводим стабильными порциями по 3200 байт
    while (m_ringBuffer.size() >= 3200) {
        QByteArray chunk = m_ringBuffer.left(3200);
        m_ringBuffer.remove(0, 3200);

        int samplesCount = chunk.size() / 2;
        const int16_t *samples = reinterpret_cast<const int16_t*>(chunk.constData());
        int32_t maxVal = 0;
        for (int i = 0; i < samplesCount; ++i) {
            if (qAbs(samples[i]) > maxVal) {
                maxVal = qAbs(samples[i]);
            }
        }

        int currentVolume = static_cast<int>((maxVal / 32767.0) * 100);
        if (currentVolume > 100) currentVolume = 100;
        emit netVolumeChanged(currentVolume);

        m_outputDevice->write(chunk);
    }
}

void AudioEngine::stop()
{
    if (m_audioSource) m_audioSource->stop();
    if (m_audioSink) m_audioSink->stop();

    if (m_tcpAudioSocket) m_tcpAudioSocket->abort();
    if (m_tcpAudioClient) {
        m_tcpAudioClient->abort();
        m_tcpAudioClient->deleteLater();
        m_tcpAudioClient = nullptr;
    }

    m_inputDevice = nullptr;
    m_outputDevice = nullptr;
    m_ringBuffer.clear();
}
