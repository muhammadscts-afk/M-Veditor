// Live, pitch-preserving audio for clips played faster/slower than normal: ffmpeg's atempo filter feeds an audio sink.
// (The media player's own speed-up changes the pitch - voices turn into "chipmunks".)
#pragma once
#include <QAudioSink>
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace mv {

class PitchAudio : public QObject {
    Q_OBJECT
public:
    explicit PitchAudio(QObject *parent = nullptr);
    ~PitchAudio() override { stop(); }
    // Starts streaming from `srcSeconds` of the file. Returns false when no audio device / ffmpeg is available.
    bool start(const QString &path, double srcSeconds, double speed, double volume);
    void stop();
    void setVolume(double v);
    bool active() const { return m_active; }
private:
    void pump();
    QProcess *m_proc = nullptr;
    QAudioSink *m_sink = nullptr;
    QIODevice *m_io = nullptr;
    QByteArray m_buf;
    QTimer m_timer;
    bool m_active = false;
};

} // namespace mv
