#include "audioengine.h"
#include <QDebug>
#include <QtMath>
#include <QMediaDevices>

AudioEngine::AudioEngine(QObject *parent)
    : QIODevice(parent), m_audioSource(nullptr), m_audioSink(nullptr), m_micVolume(0.0)
{
    m_format.setSampleRate(8000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
    open(QIODevice::ReadWrite);
}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::start() {
    stop();
    qDebug() << "=== [АКУСТИКА] Аппаратный старт звуковой платы ===";
    m_micVolume = 0.0;
    emit micVolumeChanged();

    m_audioSource = new QAudioSource(QMediaDevices::defaultAudioInput(), m_format, this);
    m_audioSource->setBufferSize(960);
    m_audioSource->start(this);

    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), m_format, this);
    m_audioSink->setBufferSize(960);
    m_audioSink->start(this);
}

void AudioEngine::stop() {
    qDebug() << "=== [АКУСТИКА] Аппаратное отключение аудиоплат ===";
    if (m_audioSource) { m_audioSource->stop(); m_audioSource->deleteLater(); m_audioSource = nullptr; }
    if (m_audioSink) { m_audioSink->stop(); m_audioSink->deleteLater(); m_audioSink = nullptr; }
    m_buffer.clear();
    m_micVolume = 0.0;
    emit micVolumeChanged();
}
void AudioEngine::playAudioBlock(const QByteArray &data) {
    if (data.isEmpty()) return;
    m_buffer.append(data);
    emit readyRead(); // Будим QAudioSink в родном потоке звуковой карты!
}

qint64 AudioEngine::writeData(const char *data, qint64 len) {
    if (len <= 0) return len;
    QByteArray audioData(data, len);

    // Вычисляем RMS
    const qint16 *samples = reinterpret_cast<const qint16*>(audioData.constData());
    int samplesCount = audioData.size() / sizeof(qint16);
    double sum = 0;
    for (int i = 0; i < samplesCount; ++i) sum += samples[i] * samples[i];
    double rms = (samplesCount > 0) ? qSqrt(sum / samplesCount) : 0;

    // Нормализуем RMS для полоски QML (переводим диапазон громкости в 0.0 - 1.0)
    double normalized = rms / 32768.0 * 15.0; // Коэффициент усиления видимости полоски
    if (normalized > 1.0) normalized = 1.0;

    if (m_micVolume != normalized) {
        m_micVolume = normalized;
        emit micVolumeChanged(); // Полоска микрофона на экране Ивана мгновенно оживёт!
    }

    emit audioDataReady(audioData);
    return len;
}

qint64 AudioEngine::readData(char *data, qint64 maxlen) {
    // НАДЁЖНЫЙ JITTER-BUFFER: Не начинаем отдавать звук динамику,
    // пока в памяти не накопится хотя бы 3 сетевых пакета (около 2500 байт).
    // Это предотвращает Buffer Underrun и засыпание аудиоплаты!
    if (m_buffer.size() < 2500) {
        memset(data, 0, maxlen); // Поддерживаем таймер аудиокарты нулями
        return maxlen;
    }

    qint64 chunk = qMin(static_cast<qint64>(m_buffer.size()), maxlen);
    memcpy(data, m_buffer.constData(), chunk);
    m_buffer.remove(0, chunk);

    return chunk;
}
