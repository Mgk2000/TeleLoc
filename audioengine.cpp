#include "audioengine.h"
#include <QDebug>

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
    // Жёстко фиксируем эталонный формат телефонии: 16000 Гц, Моно, 16-бит PCM
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // Получаем дефолтные устройства ввода и вывода
    QAudioDevice defaultInput = QMediaDevices::defaultAudioInput();
    QAudioDevice defaultOutput = QMediaDevices::defaultAudioOutput();

    // Создаём источник записи и приёмник вывода напрямую в дефолтном режиме Qt 6
    m_audioSource = new QAudioSource(defaultInput, m_format, this);
    m_audioSink = new QAudioSink(defaultOutput, m_format, this);
}

AudioEngine::~AudioEngine()
{
    stop();
}

void AudioEngine::startRecording()
{
    if (!m_audioSource) return;

    stop();

    // Открываем тракт воспроизведения звуковой карты
    m_outputDevice = m_audioSink->start();

    // Запускаем физическую запись с микрофона
    m_inputDevice = m_audioSource->start();
    if (m_inputDevice) {
        connect(m_inputDevice, &QIODevice::readyRead, this, &AudioEngine::handleInputReady);
    }
}

void AudioEngine::handleInputReady()
{
    if (!m_inputDevice) return;

    // Считываем сырые байты, оцифрованные микрофоном
    QByteArray data = m_inputDevice->readAll();
    if (!data.isEmpty()) {
        emit frameReady(data);
    }
}

void AudioEngine::playFrame(const QByteArray &frame)
{
    if (frame.isEmpty()) return;

    // Накапливаем входящий поток в эластичном кольцевом буфере звуковой карты
    m_playbackBuffer.append(frame);

    // Защита от переполнения памяти при сетевых задержках Wi-Fi
    if (m_playbackBuffer.size() > 1920) { // Более 3 кадров по 640 байт
        m_playbackBuffer.remove(0, 320); // Аккуратно прореживаем старый хвост
    }

    if (m_audioSink && m_outputDevice && m_outputDevice->isOpen()) {
        qint64 bytesFree = m_audioSink->bytesFree();

        // Квантование отдачи WASAPI/Android: скармливаем строго целыми пакетами по 320 байт,
        // что полностью уничтожает сухой фазовый треск и "эффект вертолёта"
        if (bytesFree >= 320 && m_playbackBuffer.size() >= 320) {
            qint64 bytesToWrite = qMin(static_cast<qint64>(m_playbackBuffer.size()), bytesFree);
            bytesToWrite = (bytesToWrite / 320) * 320;

            if (bytesToWrite > 0 && bytesToWrite <= m_playbackBuffer.size()) {
                m_outputDevice->write(m_playbackBuffer.constData(), bytesToWrite);
                m_playbackBuffer.remove(0, bytesToWrite);
            }
        }
        // Защита от опустошения: если данных в сети нет, шлём нули, удерживая чип карты открытым
        else if (m_playbackBuffer.size() < 320 && bytesFree >= 320) {
            QByteArray softSilence(320, 0);
            m_outputDevice->write(softSilence);
        }
    }
}

void AudioEngine::stop()
{
    if (m_audioSource) {
        m_audioSource->stop();
    }
    if (m_audioSink) {
        m_audioSink->stop();
    }
    m_playbackBuffer.clear();
    m_inputDevice = nullptr;
    m_outputDevice = nullptr;
}
