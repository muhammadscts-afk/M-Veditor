#include "PitchAudio.h"
#include "FfmpegTools.h"
#include "RenderGraph.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <algorithm>

namespace mv {

PitchAudio::PitchAudio(QObject *parent) : QObject(parent) {
    m_timer.setTimerType(Qt::PreciseTimer); m_timer.setInterval(12);
    connect(&m_timer, &QTimer::timeout, this, &PitchAudio::pump);
}

bool PitchAudio::start(const QString &path, double srcSeconds, double speed, double volume) {
    stop();
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull() || tools::ffmpegPath().isEmpty()) return false;
    QAudioFormat f; f.setSampleRate(48000); f.setChannelCount(2); f.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(f)) return false;
    m_proc = new QProcess(this);
    tools::quiet(m_proc);
    m_proc->start(tools::ffmpegPath(), RenderGraph::liveAudioArgs(path, srcSeconds, speed));
    m_sink = new QAudioSink(dev, f, this);
    m_sink->setBufferSize(48000 * 4 / 4);               // ~250 ms
    m_sink->setVolume(std::clamp(volume, 0.0, 1.0));
    m_io = m_sink->start();
    m_buf.clear();
    m_active = m_io != nullptr;
    if (m_active) m_timer.start();
    return m_active;
}

void PitchAudio::stop() {
    m_timer.stop();
    m_active = false;
    if (m_sink) { m_sink->stop(); m_sink->deleteLater(); m_sink = nullptr; m_io = nullptr; }
    if (m_proc) { m_proc->disconnect(); m_proc->kill(); m_proc->deleteLater(); m_proc = nullptr; }
    m_buf.clear();
}

void PitchAudio::setVolume(double v) { if (m_sink) m_sink->setVolume(std::clamp(v, 0.0, 1.0)); }

void PitchAudio::pump() {
    if (!m_proc || !m_sink || !m_io) return;
    m_buf += m_proc->readAllStandardOutput();
    if (m_buf.size() > 8 * 1024 * 1024) { stop(); return; }              // far ahead of the sound: stop; the preview restarts the stream when needed
    const qint64 n = std::min<qint64>(m_sink->bytesFree(), m_buf.size());
    if (n > 0) { m_io->write(m_buf.constData(), n); m_buf.remove(0, int(n)); }
}

} // namespace mv
