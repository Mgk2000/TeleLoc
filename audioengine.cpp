#include "audioengine.h"
#include <QDebug>
#include <cmath>

AudioEngine::AudioEngine(QObject *parent)
    : QIODevice(parent), m_audioSource(nullptr), m_audioSink(nullptr),
    m_speakerDevice(nullptr), m_isAudioActive(false), m_micVolume(0.0)
{
    m_format.setSampleRate(8000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // Открываем наше виртуальное устройство на запись, чтобы Qt 6 могла в него писать
    open(QIODevice::WriteOnly);

    startAudio();
}

AudioEngine::~AudioEngine() {
    stopAudio();
}

bool AudioEngine::open(OpenMode mode) {
    return QIODevice::open(mode);
}

void AudioEngine::close() {
    QIODevice::close();
}

void AudioEngine::startAudio() {
    if (m_isAudioActive) return;

    qDebug() << "=== [АКУСТИКА] Инициализация конвейера Qt 6 ===";

    m_audioSink = new QAudioSink(m_format, this);
    m_speakerDevice = m_audioSink->start();

    m_audioSource = new QAudioSource(m_format, this);
    m_audioSource->setBufferSize(960);

    // КРИТИЧЕСКИЙ ШАГ QT 6: Направляем поток микрофона прямо в наш класс!
    m_audioSource->start(this);

    m_isAudioActive = true;
    qDebug() << "=== [АКУСТИКА] Сквозной аудио-тракт Qt 6 запущен!";
}

// ЭТОТ МЕТОД В QT 6 ВЫЗЫВАЕТСЯ АВТОМАТИЧЕСКИ ПРИ НАПОЛНЕНИИ БУФЕРА МИКРОФОНА
qint64 AudioEngine::writeData(const char *data, qint64 len) {
    if (!m_isAudioActive || len <= 0) return len;

    QByteArray rawAudio(data, len);

    // МАТЕМАТИЧЕСКИЙ РАСЧЕТ RMS ГРОМКОСТИ С ПРЯМОГО УСТРОЙСТВА
    const int16_t *samples = reinterpret_cast<const int16_t*>(rawAudio.constData());
    int sampleCount = rawAudio.size() / sizeof(int16_t);

    double sum = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        double val = samples[i] / 32768.0;
        sum += val * val;
    }

    double rms = 0.0;
    if (sampleCount > 0) {
        rms = std::sqrt(sum / sampleCount);
    }

    double newVolume = qMin(1.0, rms * 5.0);
    if (std::abs(newVolume - m_micVolume) > 0.01) {
        m_micVolume = newVolume;
        emit micVolumeChanged();
    }

    // Выстреливаем байты звука в NetworkEngine
    emit audioReadyToPacket(rawAudio);

    return len;
}

void AudioEngine::handleIncomingAudio(const QByteArray &audioData) {
    if (m_isAudioActive && m_speakerDevice && m_audioSink) {
        if (m_audioSink->state() != QAudio::ActiveState) {
            m_speakerDevice = m_audioSink->start();
        }
        m_speakerDevice->write(audioData);
    }
}

void AudioEngine::stopAudio() {
    if (!m_isAudioActive) return;
    m_isAudioActive = false;
    m_micVolume = 0.0;
    emit micVolumeChanged();

    if (m_audioSource) { m_audioSource->stop(); delete m_audioSource; m_audioSource = nullptr; }
    if (m_audioSink) { m_audioSink->stop(); delete m_audioSink; m_audioSink = nullptr; }
    m_speakerDevice = nullptr;
    qDebug() << "=== [АКУСТИКА] Звук полностью заглушен.";
}
