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
    if (frame.isEmpty()) return;

    m_playbackBuffer.append(frame);

    // ИНТЕЛЛЕКТУАЛЬНОЕ ВЫРАВНИВАНИЕ КВАРЦЕВЫХ ГЕНЕРАТОРОВ:
    // Если буфер раздувается более чем на 3 кадра (1920 байт) из-за высокой скорости Ивана,
    // мы незаметно и бесшумно удаляем один самый старый микро-кадр (320 байт) из памяти буфера.
    // Это исключает резкие двойные удары по звуковой карте Windows и убирает микро-икоту!
    if (m_playbackBuffer.size() > 1920) {
        m_playbackBuffer.remove(0, 320);
    }

    if (m_audioSink && m_outputDevice && m_outputDevice->isOpen()) {
        qint64 bytesFree = m_audioSink->bytesFree();

        // Строгое квантование отдачи по целым аудио-кадрам (320 байт) для WASAPI Windows
        if (bytesFree >= 320 && m_playbackBuffer.size() >= 320) {
            qint64 bytesToWrite = qMin(static_cast<qint64>(m_playbackBuffer.size()), bytesFree);
            bytesToWrite = (bytesToWrite / 320) * 320;

            if (bytesToWrite > 0 && bytesToWrite <= m_playbackBuffer.size()) {
                m_outputDevice->write(m_playbackBuffer.constData(), bytesToWrite);
                m_playbackBuffer.remove(0, bytesToWrite);
            }
        }
        // Защита от голодания: если буфер пуст, подставляем мягкие нули, удерживая чип открытым
        else if (m_playbackBuffer.size() < 320 && bytesFree >= 320) {
            QByteArray softSilence(320, 0);
            m_outputDevice->write(softSilence);
        }
    }
}

void AudioEngine::stop()
{
    if (m_audioSource) m_audioSource->stop();
    m_inputDevice = nullptr;
    qDebug() << "Аудиозапись остановлена.";
}
