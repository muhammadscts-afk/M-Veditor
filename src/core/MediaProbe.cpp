#include "MediaProbe.h"
#include "FfmpegTools.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>

namespace mv {

bool isImagePath(const QString &path) {
    static const QStringList ext = {"png", "jpg", "jpeg", "bmp", "webp", "tif", "tiff"};
    return ext.contains(QFileInfo(path).suffix().toLower());
}

QString humanSize(qint64 b) {
    const char *u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = double(b); int i = 0;
    while (v >= 1024 && i < 4) { v /= 1024; ++i; }
    return QString::number(v, 'f', i ? 1 : 0) + " " + u[i];
}
QString humanTime(double s, bool ms) {
    if (s < 0 || std::isnan(s)) s = 0;
    int total = int(s);
    int h = total / 3600, m = (total / 60) % 60, sec = total % 60;
    QString r = h ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'))
                  : QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
    if (ms) r += "." + QString("%1").arg(int((s - total) * 1000), 3, 10, QChar('0'));
    return r;
}

static double parseRate(const QString &r) {
    const auto parts = r.split('/');
    if (parts.size() == 2) { double d = parts[1].toDouble(); return d > 0 ? parts[0].toDouble() / d : 0; }
    return r.toDouble();
}

ProbeResult parseProbeJson(const QByteArray &json, const QString &path) {
    ProbeResult r;
    r.rawJson = json;
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(json, &pe);
    if (doc.isNull() || !doc.isObject()) {
        r.error = QObject::tr("This file could not be read. It may be corrupted or is not a media file.");
        return r;
    }
    const QJsonObject root = doc.object();
    const QJsonArray streams = root["streams"].toArray();
    const QJsonObject fmt = root["format"].toObject();
    MediaItem &m = r.item;
    QFileInfo fi(path);
    m.id = newId("m"); m.path = fi.absoluteFilePath(); m.name = fi.fileName(); m.sizeBytes = fi.size();
    m.container = fmt["format_name"].toString();
    m.bitrate = fmt["bit_rate"].toString().toLongLong();
    m.duration = fmt["duration"].toString().toDouble();
    for (const auto &sv : streams) {
        const QJsonObject s = sv.toObject();
        const QString type = s["codec_type"].toString();
        if (type == "video" && !m.hasVideo && s["disposition"].toObject()["attached_pic"].toInt() != 1) {
            m.hasVideo = true;
            m.width = s["width"].toInt(); m.height = s["height"].toInt();
            m.fps = parseRate(s["avg_frame_rate"].toString());
            if (m.fps <= 0 || m.fps > 1000) m.fps = parseRate(s["r_frame_rate"].toString());
            m.videoCodec = s["codec_name"].toString();
            double rot = s["tags"].toObject()["rotate"].toString().toDouble();
            for (const auto &sd : s["side_data_list"].toArray()) if (sd.toObject().contains("rotation")) rot = sd.toObject()["rotation"].toDouble();
            if (int(std::fabs(rot)) % 180 == 90) std::swap(m.width, m.height);
            if (m.duration <= 0) m.duration = s["duration"].toString().toDouble();
        } else if (type == "audio" && !m.hasAudio) {
            m.hasAudio = true;
            m.audioCodec = s["codec_name"].toString();
            m.sampleRate = s["sample_rate"].toString().toInt();
            m.channels = s["channels"].toInt();
            if (m.duration <= 0) m.duration = s["duration"].toString().toDouble();
        }
    }
    if (!m.hasVideo && !m.hasAudio) {
        r.error = QObject::tr("No audio or video streams were found in this file.");
        return r;
    }
    if (isImagePath(path) && m.hasVideo) { m.kind = "image"; m.duration = 5.0; m.hasAudio = false; m.fps = 0; }
    else m.kind = m.hasVideo ? "video" : "audio";
    if (m.kind != "image" && m.duration <= 0) {
        r.error = QObject::tr("The duration of this file could not be determined. The file may be incomplete or damaged.");
        return r;
    }
    r.ok = true;
    return r;
}

static QStringList probeArgs(const QString &path) {
    return {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", QFileInfo(path).absoluteFilePath()};
}

ProbeResult probeBlocking(const QString &path, int timeoutMs) {
    ProbeResult r;
    QString why;
    if (!tools::available(&why)) { r.error = why; return r; }
    if (!QFileInfo::exists(path)) { r.error = QObject::tr("The file does not exist: %1").arg(path); return r; }
    QProcess p;
    tools::quiet(&p);
    p.start(tools::ffprobePath(), probeArgs(path));
    if (!p.waitForFinished(timeoutMs)) { p.kill(); r.error = QObject::tr("Reading the file took too long."); return r; }
    if (p.exitCode() != 0) {
        r.error = QObject::tr("This file is not supported or is damaged (%1).").arg(QString::fromUtf8(p.readAllStandardError()).trimmed().left(200));
        return r;
    }
    return parseProbeJson(p.readAllStandardOutput(), path);
}

void ProbeTask::start(const QString &path) {
    m_path = path;
    QString why;
    if (!tools::available(&why)) { ProbeResult r; r.error = why; emit finished(r); return; }
    m_proc = new QProcess(this);
    tools::quiet(m_proc);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        ProbeResult r;
        if (code != 0) r.error = tr("This file is not supported or is damaged (%1).").arg(QString::fromUtf8(m_proc->readAllStandardError()).trimmed().left(200));
        else r = parseProbeJson(m_proc->readAllStandardOutput(), m_path);
        emit finished(r);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) { ProbeResult r; r.error = tr("FFprobe could not be started."); emit finished(r); }
    });
    m_proc->start(tools::ffprobePath(), probeArgs(path));
}

} // namespace mv
