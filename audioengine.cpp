#include "audioengine.h"
#include "networkengine.h"
#include <unistd.h>
#include "audioengine.h"
#include <QCoreApplication> // Этого инклуда достаточно для работы QNativeInterface
#include <QJniObject>
#include <QJniEnvironment>

#include <QDir>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <opus.h>
#endif

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_audioOutputDevice(nullptr)
    , m_udpAudioReceiver(nullptr)
    , m_udpAudioSender(nullptr)
    , m_audioSource(nullptr)
    , m_audioSink(nullptr)
    , m_audioInputDevice(nullptr)
    , m_unixSizeSenderIn(0)
    , m_unixSizeSenderNet(0)
    , m_unixSizeReceiverNet(0)
    , m_unixSizeReceiverOut(0)
    , m_unixCurrentRole("none")
    , m_unixIsMuted(false)
    , m_isTalking(false)
{
    // Инициализируем аудиоформат локально
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    // Создаем аудиовыход (динамик) один раз при старте
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);

    m_udpAudioSender = new QUdpSocket(this);
    m_udpAudioReceiver = new QUdpSocket(this);

    // Биндим порт
    m_udpAudioReceiver->bind(QHostAddress::AnyIPv4, 28002);

    // ВОЗВРАЩАЕМ ВАШ НАТИВНЫЙ CONNECT К ФУНКЦИИ ЧТЕНИЯ UDP
    connect(m_udpAudioReceiver, &QUdpSocket::readyRead, this, &AudioEngine::onReadyReadUdp);
}

void AudioEngine::startRecording(const QString &targetIp)
{
    m_targetIp = targetIp;
    m_ringBuffer.clear();
    firstReceive = true;

    // Первичный перевод Android в режим связи
    setAndroidVoipMode(true);

    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }
    m_audioInputDevice = nullptr;

    if (m_audioSink) {
        m_audioSink->stop();
        m_audioOutputDevice = nullptr;
    }

    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    if (m_audioSink) {
        m_audioSink->setVolume(0.25f);
        m_audioSink->setBufferSize(35280);

        // НАШИ ИСПРАВЛЕНИЯ: сбрасываем буфер перед новым вызовом
        m_ringBuffer.clear();
        firstReceive = true;

        m_audioOutputDevice = m_audioSink->start();
    }

    if (m_opusEncoder) {
        opus_encoder_destroy(reinterpret_cast<OpusEncoder*>(m_opusEncoder));
        m_opusEncoder = nullptr;
    }
    if (m_opusDecoder) {
        opus_decoder_destroy(reinterpret_cast<OpusDecoder*>(m_opusDecoder));
        m_opusDecoder = nullptr;
    }

    int error = 0;
    m_opusEncoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
    if (error == OPUS_OK) {
        opus_encoder_ctl(reinterpret_cast<OpusEncoder*>(m_opusEncoder), OPUS_SET_BITRATE(16000));
    }
    m_opusDecoder = opus_decoder_create(16000, 1, &error);

    // Инициализируем и запускаем запись звука
    m_audioSource = new QAudioSource(QMediaDevices::defaultAudioInput(), format, this);
    m_audioInputDevice = m_audioSource->start();
    if (!m_audioInputDevice) return;

    // ИСПРАВЛЕНИЕ: Вытаскиваем сгенерированный Android Session ID из Qt 6 и принудительно включаем AEC
    int sessionId = 0;
#ifdef Q_OS_ANDROID
    sessionId = m_audioSource->property("audioSessionId").toInt();
#endif
    setAndroidVoipMode(true, sessionId);

    m_micBuffer.clear();

    connect(m_audioInputDevice, &QIODevice::readyRead, this, [this]() {
        if (!m_audioInputDevice || !m_audioSource) return;

        QByteArray data = m_audioInputDevice->readAll();
        if (data.isEmpty()) return;

        if (firstSend) {
            startWriteToFile("sender");
            firstSend = false;
        }

        if (m_unixFileSenderIn.isOpen()) {
            m_unixFileSenderIn.write(data);
        }

        m_micBuffer.append(data);

        if (m_micBuffer.size() > 3840) {
            m_micBuffer.clear();
        }

        while (m_micBuffer.size() >= 640) {
            QByteArray chunk = m_micBuffer.left(640);
            m_micBuffer.remove(0, 640);
            // --- НАЧАЛО ПОДАВЛЕНИЯ ЭХО (ЭХО-ДАМПИНГ) ---
            if (m_isOutputPlaying) {
                short *pcmData = reinterpret_cast<short*>(chunk.data());
                int samplesCount = chunk.size() / 2;
                for (int i = 0; i < samplesCount; ++i) {
                    // Ослабляем сигнал микрофона в 5 раз (на 80%), пока говорит собеседник
                    pcmData[i] = static_cast<short>(pcmData[i] * 0.2f);
                }
            }
            // --- КОНЕЦ ПОДАВЛЕНИЯ ЭХО ---

            int len = chunk.size();
            int currentVolume = 0;
            short *ptr = reinterpret_cast<short*>(chunk.data());

            for (int i = 0; i < len / 2; ++i) {
                int sample = ptr[i];
                if (sample < 0) sample = -sample;
                if (sample > currentVolume) currentVolume = sample;
            }

            currentVolume = (currentVolume * 100) / 32767;
            // --- НАЧАЛО УСИЛЕННОГО АНТИ-СВИСТА ---
            if (false && m_audioSink) {
                // Снижаем порог чувствительности до 8, чтобы ловить свист на взлете
                if (currentVolume > 8) {
                    m_audioSink->setVolume(0.0f); // Полная тишина в динамике, пока мы говорим

                    // Запоминаем время последнего громкого звука (в миллисекундах)
                    // Для Qt 6 используем QDeadlineTimer или QElapsedTimer,
                    // но проще всего привязать простой счетчик пакетов или QDateTime
                    m_lastTimeSpoken = QDateTime::currentMSecsSinceEpoch();
                } else {
                    // Если мы молчим, проверяем, прошло ли 200 мс с последнего звука.
                    // Это не даст эху из сети «поджечь» динамик обратно.
                    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
                    if (currentTime - m_lastTimeSpoken > 200) {
                        m_audioSink->setVolume(0.25f); // Возвращаем звук только после паузы
                    }
                }
            }
            // --- КОНЕЦ УСИЛЕННОГО АНТИ-СВИСТА ---
            if (m_unixFileSenderNet.isOpen()) {
                m_unixFileSenderNet.write(chunk);
                m_unixSizeSenderNet += chunk.size();
            }

            if (!m_unixIsMuted && m_udpAudioSender && m_opusEncoder) {
                unsigned char compressedData[256];
                int compressedBytes = opus_encode(
                    reinterpret_cast<OpusEncoder*>(m_opusEncoder),
                    reinterpret_cast<const opus_int16*>(chunk.constData()),
                    320,
                    compressedData,
                    sizeof(compressedData)
                    );

                if (compressedBytes > 0) {
                    m_udpAudioSender->writeDatagram(
                        reinterpret_cast<const char*>(compressedData),
                        compressedBytes,
                        QHostAddress(m_targetIp),
                        28002
                        );
                }
            }
        }
    });
}

void AudioEngine::stop()
{
    // 1. Отключаем обработку сигналов микрофона
    stopWriteToFile();
    if (m_audioInputDevice) {
        m_audioInputDevice->disconnect(this);
        m_audioInputDevice = nullptr;
    }

    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }

    if (m_audioSink) {
        m_audioSink->stop();
    }
    m_audioOutputDevice = nullptr;

    m_ringBuffer.clear();
    m_micBuffer.clear();

    if (m_unixFileSenderIn.isOpen()) m_unixFileSenderIn.close();
    if (m_unixFileSenderNet.isOpen()) m_unixFileSenderNet.close();
    if (m_unixFileReceiverNet.isOpen()) m_unixFileReceiverNet.close();
    if (m_unixFileReceiverOut.isOpen()) m_unixFileReceiverOut.close();

    setAndroidVoipMode(false);
    firstReceive = true;
    firstSend = true;
}

void AudioEngine::setAndroidVoipMode(bool enable, int audioSessionId) {
#ifdef Q_OS_ANDROID
    qDebug() << "@@@echo setAndroidVoipMode 1 enable=" << enable << "SessionID=" << audioSessionId;
    QJniEnvironment env;
    if (!env.isValid()) return;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return;

    QJniObject audioServiceString = QJniObject::getStaticObjectField(
        "android/content/Context", "AUDIO_SERVICE", "Ljava/lang/String;");
    if (!audioServiceString.isValid()) return;

    QJniObject audioManager = context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", audioServiceString.object());
    if (!audioManager.isValid()) return;

    int mode = enable ? 3 : 0; // 3 = MODE_IN_COMMUNICATION
    audioManager.callMethod<void>("setMode", "(I)V", mode);

    if (enable) {
        // На Poco громкая связь часто ломает AEC, если включена слишком рано.
        // Сначала настраиваем режим, включаем спикерфон.
        audioManager.callMethod<void>("setSpeakerphoneOn", "(Z)V", true);

        // 1. Проверяем доступность аппаратного AEC
        jboolean aecAvailable = QJniObject::callStaticMethod<jboolean>(
            "android/media/audiofx/AcousticEchoCanceler", "isAvailable");
        qDebug() << "@@@echo Аппаратный AEC доступен в Android:" << aecAvailable;

        // 2. Если AEC доступен и у нас есть валидный Audio Session ID от Qt
        if (aecAvailable && audioSessionId > 0) {
            // Вызываем статический метод создания: AcousticEchoCanceler.create(audioSessionId)
            QJniObject aecObject = QJniObject::callStaticObjectMethod(
                "android/media/audiofx/AcousticEchoCanceler",
                "create",
                "(I)Landroid/media/audiofx/AcousticEchoCanceler;",
                audioSessionId
                );

            if (aecObject.isValid()) {
                // Включаем эффект: aecObject.setEnabled(true)
                jint result = aecObject.callMethod<jint>("setEnabled", "(Z)I", true);
                qDebug() << "@@@echo Результат включения принудительного AEC:" << (result == 0 ? "УСПЕШНО" : "ОШИБКА");
            } else {
                qDebug() << "@@@echo Не удалось создать экземпляр AcousticEchoCanceler для сессии" << audioSessionId;
            }
        }
    }

    qDebug() << "@@@echo setAndroidVoipMode 7 mode=" << mode;
#else
    Q_UNUSED(enable);
    Q_UNUSED(audioSessionId);
#endif
}

void AudioEngine::onReadyReadUdp()
{
    while (m_udpAudioReceiver->hasPendingDatagrams()) {
        QByteArray chunk;
        QHostAddress senderAddress;
        quint16 senderPort;

        // Меняем размер chunk под размер входящего пакета
        chunk.resize(static_cast<int>(m_udpAudioReceiver->pendingDatagramSize()));
        int dataSize = chunk.size();

        // Читаем датаграмму из сети
        m_udpAudioReceiver->readDatagram(chunk.data(), dataSize, &senderAddress, &senderPort);
        inAudioSize += dataSize;

        if (chunk.isEmpty()) continue;

        // Декодируем Opus в PCM
        if (m_opusDecoder) {
            short decompressedPcm[320];
            int decodedSamples = opus_decode(
                reinterpret_cast<OpusDecoder*>(m_opusDecoder),
                reinterpret_cast<const unsigned char*>(chunk.constData()),
                chunk.size(),
                decompressedPcm,
                320,
                0
                );
            if (decodedSamples > 0) {
                chunk = QByteArray(reinterpret_cast<const char*>(decompressedPcm), decodedSamples * 2);
            }
        }

        // Эхо-тест
        if (m_isEchoTestMode && m_udpAudioSender) {
            m_udpAudioSender->writeDatagram(chunk, senderAddress, 28002);
        }

        // Запись отладочного файла сети
        if (m_unixFileReceiverNet.isOpen()) {
            m_unixFileReceiverNet.write(chunk);
            m_unixSizeReceiverNet += chunk.size();
        }

        // Подсчет уровня громкости (у вас в цикле было вычисление максимума)
        int len = chunk.size();
        int currentVolume = 0;
        const short *ptr = reinterpret_cast<const short*>(chunk.constData());
        for (int i = 0; i < len / 2; ++i) {
            int sample = ptr[i];
            if (sample < 0) sample = -sample;
            if (sample > currentVolume) currentVolume = sample;
        }
        currentVolume = (currentVolume * 100) / 32767;
        emit netVolumeUpdated(currentVolume);

        // Запись второго отладочного файла
        if (m_unixFileReceiverOut.isOpen()) {
            m_unixFileReceiverOut.write(chunk);
            m_unixSizeReceiverOut += chunk.size();
        }
        // --- ФИЛЬТР НИЖНИХ ЧАСТОТ 2-ГО ПОРЯДКА (БАТТЕРВОРТ, СРЕЗ ~3.8 КГц) ---
        // Коэффициенты разностного уравнения фильтра
        const float b0 = 0.3012f;
        const float b1 = 0.6025f;
        const float b2 = 0.3012f;
        const float a1 = -0.4908f;
        const float a2 = 0.6958f;

        short *pcmPtr = reinterpret_cast<short*>(chunk.data());
        int pcmSamplesCount = chunk.size() / 2;

        for (int i = 0; i < pcmSamplesCount; ++i) {
            float x0 = static_cast<float>(pcmPtr[i]);

            // Формула фильтра второго порядка (прямая форма I)
            float y0 = (b0 * x0) + (b1 * m_x1) + (b2 * m_x2) - (a1 * m_y1) - (a2 * m_y2);

            // Сдвигаем историю сэмплов
            m_x2 = m_x1;
            m_x1 = x0;
            m_y2 = m_y1;
            m_y1 = y0;

            // Записываем отфильтрованный результат обратно в PCM фрейм
            pcmPtr[i] = static_cast<short>(y0);
        }
        // --- КОНЕЦ ФИЛЬТРА 2-ГО ПОРЯДКА ---
        // КЛЮЧЕВОЙ МОМЕНТ: вместо m_audioOutputDevice->write(chunk) делаем:
        m_ringBuffer.append(chunk);
        processAudioOutput();
    }
}

void AudioEngine::muteMicrophone(bool mute)
{
    m_unixIsMuted = mute;
}


#include <QDir>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <unistd.h>
#endif

void AudioEngine::startWriteToFile(const QString &role)
{
    qDebug() << "@@@  startWriteToFile 1" << role;;
    m_unixCurrentRole = role;
    QByteArray dummyHeader;
    dummyHeader.resize(44);

    QString musicPath;

#ifdef Q_OS_ANDROID
    // --- ПЛАТФОРМЕННЫЙ КОД ДЛЯ ANDROID (ЧЕРЕЗ ДЕСКРИПТОРЫ) ---
    auto getAndroidNativeFd = [](const QString &fileName) -> int {
        qDebug() << "@@@  startWriteToFile 2"  ;

        QJniObject publicDir = QJniObject::callStaticObjectMethod(
            "android/os/Environment",
            "getExternalStoragePublicDirectory",
            "(Ljava/lang/String;)Ljava/io/File;",
            QJniObject::getStaticObjectField("android/os/Environment", "DIRECTORY_MUSIC", "Ljava/lang/String;").object()
            );
        qDebug() << "@@@  startWriteToFile 3 dir =";  ;

        if (!publicDir.isValid()) return -1;

        QJniObject javaFile("java/io/File", "(Ljava/io/File;Ljava/lang/String;)V",
                            publicDir.object(), QJniObject::fromString(fileName).object());
        qDebug() << "@@@  startWriteToFile 4 ";  ;

        QJniObject fos("java/io/FileOutputStream", "(Ljava/io/File;)V", javaFile.object());
        if (!fos.isValid()) return -1;
        qDebug() << "@@@  startWriteToFile 5 ";  ;

        QJniObject fdObj = fos.callObjectMethod("getFD", "()Ljava/io/FileDescriptor;");
        qDebug() << "@@@  startWriteToFile 6 ";  ;

        if (!fdObj.isValid()) return -1;

        jint nativeFd = fdObj.getField<jint>("descriptor");
        qDebug() << "@@@  startWriteToFile 7 ";  ;

        return ::dup(static_cast<int>(nativeFd));
    };

    if (true||m_unixCurrentRole == "sender") {
        m_unixSizeSenderIn = 0; m_unixSizeSenderNet = 0;
        int fdIn = getAndroidNativeFd("SenderIn.wav");
        qDebug() << "@@@  startWriteToFile 8 ";  ;

        if (fdIn >= 0) m_unixFileSenderIn.open(fdIn, QIODevice::WriteOnly | QIODevice::Truncate);
        qDebug() << "@@@  startWriteToFile 9 ";  ;

        int fdNet = getAndroidNativeFd("SenderNet.wav");
        qDebug() << "@@@  startWriteToFile 10 fdnet="  << fdNet;
        if (fdNet >= 0) m_unixFileSenderNet.open(fdNet, QIODevice::WriteOnly | QIODevice::Truncate);
        qDebug() << "@@@  startWriteToFile 11 ";  ;

    }
    if (true || m_unixCurrentRole == "receiver") {
        qDebug() << "@@@  startWriteToFile 12 ";  ;

        m_unixSizeReceiverNet = 0; m_unixSizeReceiverOut = 0;
        int fdRecNet = getAndroidNativeFd("ReceiverNet.wav");
        qDebug() << "@@@  startWriteToFile 13 fdnet="  << fdRecNet;
        if (fdRecNet >= 0) m_unixFileReceiverNet.open(fdRecNet, QIODevice::WriteOnly | QIODevice::Truncate);
        int fdRecOut = getAndroidNativeFd("ReceiverOut.wav");
        qDebug() << "@@@  startWriteToFile 14 fdnet="  << fdRecNet;
        if (fdRecOut >= 0) m_unixFileReceiverOut.open(fdRecOut, QIODevice::WriteOnly | QIODevice::Truncate);
    }
    qDebug() << "@@@ [AudioEngine] Android: Нативная запись запущена.";

#else
    // --- СТАНДАРТНЫЙ КРОСС ПЛАТФОРМЕННЫЙ КОД (ДЛЯ WINDOWS) ---
    musicPath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    QDir().mkpath(musicPath);

    if (m_unixCurrentRole == "sender") {
        m_unixSizeSenderIn = 0; m_unixSizeSenderNet = 0;
        m_unixFileSenderIn.setFileName(musicPath + "/SenderIn.wav");
        m_unixFileSenderIn.open(QIODevice::WriteOnly | QIODevice::Truncate);
        m_unixFileSenderNet.setFileName(musicPath + "/SenderNet.wav");
        m_unixFileSenderNet.open(QIODevice::WriteOnly | QIODevice::Truncate);
    }
    else if (m_unixCurrentRole == "receiver") {
        m_unixSizeReceiverNet = 0; m_unixSizeReceiverOut = 0;
        m_unixFileReceiverNet.setFileName(musicPath + "/ReceiverNet.wav");
        m_unixFileReceiverNet.open(QIODevice::WriteOnly | QIODevice::Truncate);
        m_unixFileReceiverOut.setFileName(musicPath + "/ReceiverOut.wav");
        m_unixFileReceiverOut.open(QIODevice::WriteOnly | QIODevice::Truncate);
    }
    qDebug() << "@@@ [AudioEngine] Windows: Запись запущена по пути:" << musicPath;
#endif

    // Записываем пустые заголовки
    if (m_unixCurrentRole == "sender") {
        if (m_unixFileSenderIn.isOpen()) m_unixFileSenderIn.write(dummyHeader);
        qDebug() << "@@@  startWriteToFile 15 ";

        if (m_unixFileSenderNet.isOpen()) m_unixFileSenderNet.write(dummyHeader);
        qDebug() << "@@@  startWriteToFile 16 ";

    } else if (m_unixCurrentRole == "receiver") {
        if (m_unixFileReceiverNet.isOpen()) m_unixFileReceiverNet.write(dummyHeader);
        qDebug() << "@@@  startWriteToFile 17 ";

        if (m_unixFileReceiverOut.isOpen()) m_unixFileReceiverOut.write(dummyHeader);
        qDebug() << "@@@  startWriteToFile 18 ";

    }
}

void AudioEngine::stopWriteToFile()
{
        if (m_unixFileSenderIn.isOpen()) { writeWavHeader(m_unixFileSenderIn, m_unixSizeSenderIn); m_unixFileSenderIn.close(); }
        if (m_unixFileSenderNet.isOpen()) { writeWavHeader(m_unixFileSenderNet, m_unixSizeSenderNet); m_unixFileSenderNet.close(); }
        if (m_unixFileReceiverNet.isOpen()) { writeWavHeader(m_unixFileReceiverNet, m_unixSizeReceiverNet); m_unixFileReceiverNet.close(); }
        if (m_unixFileReceiverOut.isOpen()) { writeWavHeader(m_unixFileReceiverOut, m_unixSizeReceiverOut); m_unixFileReceiverOut.close(); }
    m_unixCurrentRole = "none";
        firstReceive = true;
    firstSend = true;
    qDebug() << "@@@ [AudioEngine] Все активные аудиофайлы успешно сохранены.";
}

void AudioEngine::writeWavHeader(QFile &file, int dataSize)
{
    file.seek(0);
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    out.writeRawData("RIFF", 4);
    out << static_cast<quint32>(36 + dataSize);
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << static_cast<quint32>(16);
    out << static_cast<quint16>(1);
    out << static_cast<quint16>(1);
    out << static_cast<quint32>(16000);
    out << static_cast<quint32>(32000);
    out << static_cast<quint16>(2);
    out << static_cast<quint16>(16);
    out.writeRawData("data", 4);
    out << static_cast<quint32>(dataSize);
}

void AudioEngine::startAudioTimer()
{
//    connect(audioTimer, SIGNAL(timeout()), this, SLOT(onTimer()) );
    connect(&audioTimer, SIGNAL(timeout()), this, SLOT(onTimer()) );
audioTimer.start(1000);
}

void AudioEngine::onTimer()
{
    qDebug() << "@@@audio in=" << inAudioSize << "out=" << outAuioSize << "audidevice=" <<
        !! m_audioInputDevice << !!m_audioOutputDevice;
    inAudioSize = 0;
    outAuioSize = 0;
}

void AudioEngine::enableEchoTest(bool enable)
{
    m_isEchoTestMode = enable;

    // Если мы стали эхо-зеркалом, нам нужно открыть динамик на запись холостых данных
    if (m_isEchoTestMode) {
        setAndroidVoipMode(true);
        if (m_audioSink) {
            m_audioSink->setVolume(0.125f);
            m_audioOutputDevice = m_audioSink->start();
        }
    }
}
void AudioEngine::processAudioOutput()
{
    if (!m_audioOutputDevice || !m_audioOutputDevice->isOpen() || !m_audioSink) {
        return;
    }

    const int frameSize = 640;

    if (firstReceive) {
        if (m_ringBuffer.size() < (frameSize * 4)) {
            return;
        }
        firstReceive = false;
    }

    if (m_ringBuffer.size() > (frameSize * 25)) {
        m_ringBuffer.remove(0, m_ringBuffer.size() - (frameSize * 25));
    }

    bool bytesWritten = false;
    while (m_ringBuffer.size() >= frameSize && m_audioSink->bytesFree() >= frameSize) {
        QByteArray chunk = m_ringBuffer.left(frameSize);
        m_ringBuffer.remove(0, frameSize);

        m_audioOutputDevice->write(chunk);
        bytesWritten = true;
    }

    // Запоминаем: если мы только что что-то сыграли, значит динамик активен
    m_isOutputPlaying = bytesWritten;
}
