#ifndef TTSENGINE_H
#define TTSENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QMediaPlayer;
QT_END_NAMESPACE

/**
 * TTS engine using sherpa-onnx (in-process, no Piper CLI).
 * Requires sherpa-onnx C API: model (.onnx), tokens (.txt), and espeak-ng-data dir for Piper/VITS.
 */
class TtsEngine : public QObject
{
    Q_OBJECT
public:
    /** State: 0=Ready, 1=Speaking, 2=Paused, 3=BackendError, 4=Loading (synthesizing). */
    enum State { Ready = 0, Speaking = 1, Paused = 2, BackendError = 3, Loading = 4 };

    explicit TtsEngine(QObject *parent = nullptr);
    ~TtsEngine();

    /** Path to VITS/Piper ONNX model (.onnx). */
    void setModelPath(const QString &path);
    /** Path to tokens file (e.g. tokens.txt). */
    void setTokensPath(const QString &path);
    /** Path to espeak-ng-data directory (required for Piper models). */
    void setDataDir(const QString &path);

    /** Initialize sherpa-onnx TTS (validate paths, load model). Returns true if ready. */
    bool initialize();

    bool isAvailable() const;

    /** Set TTS speed as a percent (50..200). 100 = default. */
    void setSpeedPercent(int percent);
    int speedPercent() const;

    void speak(const QString &text);
    void pause();
    void resume();
    void stop();

    /** Synthesize text to a WAV file (does not start playback). */
    void exportWav(const QString &text, const QString &wavPath);
    /** Synthesize multiple chunks and concatenate into one WAV (does not start playback). */
    void exportWavChunks(const QStringList &chunks, const QString &wavPath, int silenceMsBetweenChunks = 120);

    int state() const;

signals:
    void stateChanged(int state);
    /** Emitted when synthesis finished and WAV is ready to play (path valid only on main thread). */
    void synthesisFinished(const QString &wavPath);
    /** Emitted when exportWav() finishes. */
    void exportFinished(bool ok, const QString &wavPath, const QString &errorMessage);

private:
    void setState(int state);
    void onSynthesisFinished(const QString &wavPath);
    void onMediaStateChanged(int newState);
    void onMediaStatusChanged(int status);
    QString defaultModelPath() const;
    QString defaultTokensPath() const;
    QString defaultDataDir() const;

    QString synthesizeToWavPath(const QString &text, const QString &desiredWavPath) const;

    QString m_modelPath;
    QString m_tokensPath;
    QString m_dataDir;
    QMediaPlayer *m_mediaPlayer = nullptr;
    QString m_currentWavPath;
    int m_state = Ready;
    bool m_available = false;
    int m_speedPercent = 100;
};

#endif // TTSENGINE_H
