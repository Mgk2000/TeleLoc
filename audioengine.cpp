#include "audioengine.h"
#include <unistd.h>

#include <QDir>
#include <QStandardPaths>
AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_udpAudioReceiver(nullptr)
    , m_udpAudioSender(nullptr)
    , m_audioSource(nullptr)
    , m_audioSink(nullptr)
    , m_audioInputDevice(nullptr)
    , m_audioOutputDevice(nullptr)
{
    m_udpAudioReceiver = new QUdpSocket(this);
    m_udpAudioSender = new QUdpSocket(this);

    if (!m_udpAudioReceiver->bind(QHostAddress::AnyIPv4, 28002, QUdpSocket::ShareAddress)) {
        qWarning() << "Failed to bind UDP audio port";
    }

    connect(m_udpAudioReceiver, &QUdpSocket::readyRead, this, &AudioEngine::onReadyReadUdp);

    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    m_audioSource = new QAudioSource(QMediaDevices::defaultAudioInput(), format, this);
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);

    m_audioOutputDevice = m_audioSink->start();
    m_unixSizeSenderIn = 0;
    m_unixSizeSenderNet = 0;
    m_unixSizeReceiverNet = 0;
    m_unixSizeReceiverOut = 0;
    m_unixCurrentRole = "none";    m_unixIsMuted = false;
}
void AudioEngine::startRecording(const QString &targetIp)
{
    m_targetIp = targetIp;

    if (m_audioInputDevice) {
        m_audioSource->stop();
    }

    m_audioInputDevice = m_audioSource->start();

    if (m_audioInputDevice) {
        connect(m_audioInputDevice, &QIODevice::readyRead, this, [this]() {
            QByteArray data = m_audioInputDevice->readAll();
            if (data.isEmpty()) return;

            // --- ЭТАП 1: Чистый звук из микрофона (SenderIn) ---
            if (m_unixCurrentRole == "sender" && m_unixFileSenderIn.isOpen()) {
                m_unixFileSenderIn.write(data);
                m_unixSizeSenderIn += data.size();
            }

            int len = data.size();
            int currentVolume = 0;
            short *ptr = reinterpret_cast<short*>(data.data());

            for (int i = 0; i < len / 2; ++i) {
                int sample = ptr[i];
                if (sample < 0) sample = -sample;
                if (sample > currentVolume) currentVolume = sample;

                int reduced = static_cast<int>(ptr[i] / 2.5);
                if (reduced > 32767) reduced = 32767;
                if (reduced < -32768) reduced = -32768;
                ptr[i] = static_cast<short>(reduced);
            }

            currentVolume = (currentVolume * 100) / 32767;
            emit micVolumeUpdated(currentVolume);

            // --- ЭТАП 2: Модифицированный звук перед отправкой в сеть (SenderNet) ---
            if (m_unixCurrentRole == "sender" && m_unixFileSenderNet.isOpen()) {
                m_unixFileSenderNet.write(data);
                m_unixSizeSenderNet += data.size();
            }

            if (!m_unixIsMuted) {
                m_udpAudioSender->writeDatagram(data, QHostAddress(m_targetIp), 28002);
            }
        });
    }
}
void AudioEngine::stop()
{
    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_audioInputDevice = nullptr;
    m_ringBuffer.clear();
}
void AudioEngine::onReadyReadUdp()
{
    while (m_udpAudioReceiver->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_udpAudioReceiver->pendingDatagramSize()));
        m_udpAudioReceiver->readDatagram(datagram.data(), datagram.size());

        m_ringBuffer.append(datagram);

        if (m_ringBuffer.size() > 19200) {
            m_ringBuffer.remove(0, m_ringBuffer.size() - 3200);
        }

        while (m_ringBuffer.size() >= 3200) {
            QByteArray chunk = m_ringBuffer.left(3200);
            m_ringBuffer.remove(0, 3200);

            // --- ЭТАП 3: Звук в том виде, в каком он пришёл из сети (ReceiverNet) ---
            if (m_unixCurrentRole == "receiver" && m_unixFileReceiverNet.isOpen()) {
                m_unixFileReceiverNet.write(chunk);
                m_unixSizeReceiverNet += chunk.size();
            }

            int len = chunk.size();
            int currentVolume = 0;
            const short *ptr = reinterpret_cast<const short*>(chunk.constData());

            for (int i = 0; i < len / 2; ++i) {
                int sample = ptr[i];
                if (sample < 0) sample = -sample;
                if (sample > currentVolume) currentVolume = sample;
            }
            currentVolume = (currentVolume * 100) / 32767;
            if (currentVolume > 0)
                qDebug() << "@@@ggg vol=" << currentVolume;
            emit netVolumeUpdated   (currentVolume);

            // --- ЭТАП 4: Звук после всех обработок перед отправкой в динамик (ReceiverOut) ---
            if (m_unixCurrentRole == "receiver" && m_unixFileReceiverOut.isOpen()) {
                m_unixFileReceiverOut.write(chunk);
                m_unixSizeReceiverOut += chunk.size();
            }

            if (m_audioOutputDevice && m_audioOutputDevice->isOpen()) {
                m_audioOutputDevice->write(chunk);
            }
        }
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

    if (m_unixCurrentRole == "sender") {
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
    else if (m_unixCurrentRole == "receiver") {
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
    if (m_unixCurrentRole == "sender") {
        if (m_unixFileSenderIn.isOpen()) { writeWavHeader(m_unixFileSenderIn, m_unixSizeSenderIn); m_unixFileSenderIn.close(); }
        if (m_unixFileSenderNet.isOpen()) { writeWavHeader(m_unixFileSenderNet, m_unixSizeSenderNet); m_unixFileSenderNet.close(); }
    }
    else if (m_unixCurrentRole == "receiver") {
        if (m_unixFileReceiverNet.isOpen()) { writeWavHeader(m_unixFileReceiverNet, m_unixSizeReceiverNet); m_unixFileReceiverNet.close(); }
        if (m_unixFileReceiverOut.isOpen()) { writeWavHeader(m_unixFileReceiverOut, m_unixSizeReceiverOut); m_unixFileReceiverOut.close(); }
    }
    m_unixCurrentRole = "none";
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
