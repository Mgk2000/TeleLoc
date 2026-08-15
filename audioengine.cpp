#include "audioengine.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QHostAddress>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
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
// Вспомогательная функция для получения чистого пути к файлу test.wav
static QString getWavFilePath() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(configDir); // Гарантируем, что папка существует
    return configDir + "/test.wav";
}

void AudioEngine::startRecording() {
    if (m_isRecording) return;

    QString filePath = getWavFilePath();
    m_recordFile.setFileName(filePath);

    // Открываем файл. Сначала пишем пустые 44 байта, чтобы зарезервировать место под WAV-заголовок
    if (m_recordFile.open(QIODevice::WriteOnly)) {
        qDebug() << "@@@ ЗВУК C++: Начало записи в файл:" << filePath;
        m_recordFile.write(QByteArray(44, 0));
        m_isRecording = true;
    } else {
        qDebug() << "@@@ ЗВУК C++ ОШИБКА: Не удалось открыть файл для записи:" << m_recordFile.errorString();
    }
}

void AudioEngine::stopRecording() {
    if (!m_isRecording) return;
    m_isRecording = false;

    // Считаем точный размер накопленных аудио-данных
    quint32 dataSize = m_recordFile.size() - 44;

    // Формируем честный системный WAV-заголовок
    WAVHeader header;
    header.chunkSize = 36 + dataSize;
    header.subchunk2Size = dataSize;

    // Настройки частоты (замените 16000 на вашу частоту, если в рации используется 8000 или 44100)
    header.sampleRate = 16000;
    header.bitsPerSample = 16;
    header.numChannels = 1;
    header.byteRate = header.sampleRate * header.numChannels * (header.bitsPerSample / 8);
    header.blockAlign = header.numChannels * (header.bitsPerSample / 8);

    // Возвращаемся в самое начало файла (на нулевой байт) и перезаписываем пустые байты реальной структурой
    if (m_recordFile.seek(0)) {
        m_recordFile.write(reinterpret_cast<const char*>(&header), 44);
        qDebug() << "@@@ ЗВУК C++: Запись остановлена. Заголовок WAV успешно сформирован. Размер данных:" << dataSize;
    }

    m_recordFile.close();
}

void AudioEngine::playRecordedFile() {
    QString filePath = getWavFilePath();
    m_playFile.setFileName(filePath);

    if (!m_playFile.open(QIODevice::ReadOnly)) {
        qDebug() << "@@@ ЗВУК C++ ОШИБКА: Не удалось открыть записанный файл для чтения:" << m_playFile.errorString();
        return;
    }

    // Пропускаем 44 байта заголовка, чтобы воспроизводить чистый PCM-звук
    m_playFile.seek(44);

    qDebug() << "@@@ ЗВУК C++: Начинаю воспроизведение записанного test.wav...";

    // Используем стандартный формат рации
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();

    // Удаляем старый плеер, если он остался в памяти
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
    }

    m_audioSink = new QAudioSink(outputDevice, format, this);

    // Соединяем сигнал окончания файла, чтобы вовремя закрыть его
    connect(m_audioSink, &QAudioSink::stateChanged, this, [this](QAudio::State newState) {
        if (newState == QAudio::IdleState || newState == QAudio::StoppedState) {
            m_audioSink->stop();
            m_playFile.close();
            qDebug() << "@@@ ЗВУК C++: Воспроизведение test.wav завершено, файл закрыт.";
        }
    });

    // Напрямую скармливаем файл аудио-выходу девайса Петра
    m_audioSink->start(&m_playFile);
}
void AudioEngine::writeAudioFrame(const QByteArray &data) {
    if (m_isRecording && m_recordFile.isOpen()) {
        m_recordFile.write(data);
    }
}

