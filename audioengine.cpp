#include "audioengine.h"
#include <QDebug>

AudioEngine::AudioEngine(QObject *parent) : QObject(parent)
{
    // Настраиваем стандартный формат аудио для рации (16kHz, моно, 16 бит PCM)
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // Инициализируем устройства ввода и вывода звука для Qt 6
    m_audioSource = new QAudioSource(QMediaDevices::defaultAudioInput(), m_format, this);
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), m_format, this);

    // Выделяем буфер под воспроизведение пакетов
    m_outputDevice = m_audioSink->start();
}

AudioEngine::~AudioEngine()
{
    stop();
}

void AudioEngine::startRecording()
{
    if (m_inputDevice) return;

    m_inputDevice = m_audioSource->start();
    if (m_inputDevice) {
        connect(m_inputDevice, &QIODevice::readyRead, this, &AudioEngine::handleInputReady);
        qDebug() << "Микрофон успешно запущен.";
    }
}

void AudioEngine::handleInputReady()
{
    if (!m_inputDevice) return;

    // Считываем аудиоданные с микрофона и сразу шлем их в сеть
    QByteArray data = m_inputDevice->readAll();
    if (!data.isEmpty()) {
        emit frameReady(data);
    }
}

void AudioEngine::playFrame(const QByteArray &frame)
{
    if (m_outputDevice && !frame.isEmpty()) {
        // Записываем полученный из сети кадр напрямую в динамик
        m_outputDevice->write(frame);
    }
}

void AudioEngine::stop()
{
    if (m_audioSource) m_audioSource->stop();
    m_inputDevice = nullptr;
    qDebug() << "Аудиозапись остановлена.";
}
