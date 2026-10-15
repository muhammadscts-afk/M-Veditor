#include "ExportJob.h"
#include "FfmpegTools.h"
#include "MediaProbe.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStorageInfo>
#include <QTimer>
#include <cmath>

namespace mv {

ExportJob::ExportJob(QObject *parent) : QObject(parent) {}
ExportJob::~ExportJob() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) { m_proc->kill(); m_proc->waitForFinished(2000); }
    cleanupPart();
}

QString ExportJob::friendlyError(const QString &e) {
    const QString s = e.toLower();
    if (s.contains("no space left")) return tr("The disk is full. Free some space or choose another folder.");
    if (s.contains("permission denied")) return tr("Windows denied access to the output location. Choose a folder you can write to.");
    if (s.contains("no such file") || s.contains("could not open")) return tr("A source file could not be opened. It may have been moved or deleted.");
    if (s.contains("invalid data found")) return tr("A source file is damaged or incomplete.");
    if (s.contains("unknown encoder") || s.contains("encoder not found")) return tr("The required encoder is not available in this build.");
    if (s.contains("cannot load nvcuda") || s.contains("nvenc") || s.contains("amf") || s.contains("qsv") || s.contains("device"))
        return tr("The hardware encoder is not available on this computer.");
    if (s.contains("fontconfig") || s.contains("drawtext")) return tr("The title font could not be loaded.");
    return tr("The render stopped unexpectedly.");
}

static double estimateBytes(const RenderPlan &plan, const ExportOptions &o, int w, int h) {
    double bps;
    if (o.format == "wav") bps = o.sampleRate * (o.channels == 1 ? 1 : 2) * 16.0;
    else if (o.format == "flac") bps = o.sampleRate * (o.channels == 1 ? 1 : 2) * 16.0 * 0.7;
    else if (o.format == "mp3" || o.format == "aac") bps = o.audioBitrateKbps * 1000.0;
    else if (o.videoBitrateKbps > 0) bps = (o.videoBitrateKbps + o.audioBitrateKbps) * 1000.0;
    else {
        static const double q[] = {0.04, 0.08, 0.15, 0.4};
        int qi = o.quality == "draft" ? 0 : o.quality == "high" ? 2 : o.quality == "max" ? 3 : 1;
        bps = w * double(h) * std::max(24.0, o.fps) * q[qi] + o.audioBitrateKbps * 1000.0;
    }
    return bps / 8.0 * plan.duration * 1.25 + 20.0 * 1024 * 1024;
}

bool ExportJob::start(const Project &project, const ExportOptions &options, QString *error) {
    if (m_running) { if (error) *error = tr("Another export is already running."); return false; }
    m_project = project; m_opts = options; m_cancelled = m_done = m_triedHw = m_retrying = false; m_lastFraction = 0;
    const QStringList problems = RenderGraph::validate(project, options);
    if (!problems.isEmpty()) { if (error) *error = problems.join("\n"); return false; }
    QFileInfo out(options.outputPath);
    QDir dir = out.absoluteDir();
    if (!dir.exists() && !dir.mkpath(".")) { if (error) *error = tr("The output folder could not be created: %1").arg(dir.path()); return false; }
    // writable?
    const FormatInfo fmt = formatById(options.format);
    m_part = dir.absoluteFilePath(out.completeBaseName() + ".mveditor-part." + (fmt.ext));
    {
        QFile t(m_part);
        if (!t.open(QIODevice::WriteOnly)) { if (error) *error = tr("Cannot write to \"%1\": %2").arg(dir.path(), t.errorString()); return false; }
        t.close(); QFile::remove(m_part);
    }
    m_opts.partPath = m_part;
    m_tmp = std::make_unique<QTemporaryDir>();
    if (!m_tmp->isValid()) { if (error) *error = tr("A temporary working folder could not be created."); return false; }
    return launch(true, error);
}

bool ExportJob::launch(bool allowHardware, QString *error) {
    m_plan = RenderGraph::build(m_project, m_opts, allowHardware);
    if (!m_plan.ok) { if (error) *error = m_plan.errors.join("\n"); return false; }
    if (!m_retrying) {
        const int w = m_opts.width ? m_opts.width : m_project.settings.width, h = m_opts.height ? m_opts.height : m_project.settings.height;
        const qint64 need = qint64(estimateBytes(m_plan, m_opts, w, h));
        QStorageInfo si(QFileInfo(m_opts.outputPath).absolutePath());
        if (si.isValid() && si.bytesAvailable() > 0 && si.bytesAvailable() < need) {
            if (error) *error = tr("Not enough free disk space. About %1 is needed but only %2 is available.").arg(humanSize(need), humanSize(si.bytesAvailable()));
            return false;
        }
    }
    for (auto &f : m_plan.files) {
        QFile file(m_tmp->filePath(f.first));
        if (!file.open(QIODevice::WriteOnly)) { if (error) *error = tr("Could not write a temporary file."); return false; }
        file.write(f.second);
    }
    m_triedHw = m_plan.usedHardware;
    m_buf.clear(); m_stderrTail.clear();
    m_proc = std::make_unique<QProcess>();
    tools::quiet(m_proc.get());
    m_proc->setWorkingDirectory(m_tmp->path());
    connect(m_proc.get(), &QProcess::readyReadStandardOutput, this, &ExportJob::onStdout);
    connect(m_proc.get(), &QProcess::readyReadStandardError, this, &ExportJob::onStderr);
    connect(m_proc.get(), &QProcess::finished, this, &ExportJob::onFinished);
    connect(m_proc.get(), &QProcess::errorOccurred, this, &ExportJob::onProcError);
    m_clock.start();
    m_running = true;
    emit logLine("ffmpeg " + m_plan.args.join(' '));
    m_proc->start(tools::ffmpegPath(), m_plan.args);
    return true;
}

void ExportJob::onStdout() {
    m_buf += QString::fromUtf8(m_proc->readAllStandardOutput());
    int nl;
    double us = -1, speed = 0;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QString line = m_buf.left(nl).trimmed();
        m_buf.remove(0, nl + 1);
        if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) { bool ok; double v = line.section('=', 1).toDouble(&ok); if (ok) us = v; }
        else if (line.startsWith("speed=")) speed = line.section('=', 1).remove('x').trimmed().toDouble();
        else if (line.startsWith("progress=") && us >= 0 && m_plan.duration > 0) {
            const double cur = us / 1e6;
            const double frac = std::min(1.0, std::max(0.0, cur / m_plan.duration));
            double eta = -1;
            if (speed > 0.01) eta = (m_plan.duration - cur) / speed;
            else if (frac > 0.02) eta = m_clock.elapsed() / 1000.0 * (1 - frac) / frac;
            m_lastFraction = frac;
            emit progress(frac, eta, tr("Rendering… %1%").arg(int(frac * 100)));
        }
    }
}
void ExportJob::onStderr() {
    const QString t = QString::fromUtf8(m_proc->readAllStandardError());
    m_stderrTail = (m_stderrTail + t).right(4000);
    for (auto &l : t.split('\n', Qt::SkipEmptyParts)) emit logLine(l);
}
void ExportJob::onProcError(QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart && m_running) { m_running = false; finishWith(false, tr("FFmpeg could not be started. Please reinstall M Veditor.")); }
}

void ExportJob::cancel() {
    if (!m_running || !m_proc) return;
    m_cancelled = true;
    m_proc->write("q\n");                       // graceful stop
    QTimer::singleShot(3000, this, [this] { if (m_proc && m_proc->state() != QProcess::NotRunning) m_proc->kill(); });
}

void ExportJob::cleanupPart() { if (!m_part.isEmpty()) QFile::remove(m_part); }

void ExportJob::finishWith(bool ok, const QString &msg) {
    if (m_done) return;
    m_done = true; m_running = false;
    if (!ok) cleanupPart();
    emit finished(ok, msg, m_opts.outputPath);
}

void ExportJob::onFinished(int code, QProcess::ExitStatus st) {
    if (m_done) return;
    m_running = false;
    onStdout();
    if (m_cancelled) { finishWith(false, tr("The export was cancelled. No file was created.")); return; }
    if (st != QProcess::NormalExit || code != 0) {
        if (m_triedHw && !m_retrying) {                 // reliable software fallback
            emit logLine("Hardware encoder failed, retrying with the software encoder.");
            m_retrying = true;
            cleanupPart();
            QString err;
            if (launch(false, &err)) { emit progress(0, -1, tr("Hardware encoder unavailable – using software encoder…")); return; }
            finishWith(false, err);
            return;
        }
        const QString tail = m_stderrTail.trimmed().split('\n').last().left(300);
        finishWith(false, friendlyError(m_stderrTail) + (tail.isEmpty() ? QString() : "\n\n" + tr("Details: %1").arg(tail)));
        return;
    }
    // verify the temporary file before it becomes the real output
    emit progress(1.0, 0, tr("Verifying the result…"));
    const QString bad = verifyOutput(m_part, m_opts, m_plan.duration, formatById(m_opts.format).video, m_plan.hasAudio);
    if (!bad.isEmpty()) { finishWith(false, tr("The export finished but the file did not pass verification: %1").arg(bad)); return; }
    QFile::remove(m_opts.outputPath);
    if (!QFile::rename(m_part, m_opts.outputPath)) {
        finishWith(false, tr("The file was rendered but could not be moved to \"%1\". It may be in use by another program.").arg(m_opts.outputPath));
        return;
    }
    m_part.clear();
    finishWith(true, tr("Export complete: %1").arg(QDir::toNativeSeparators(m_opts.outputPath)));
}

QString ExportJob::verifyOutput(const QString &path, const ExportOptions &o, double expected, bool expectVideo, bool expectAudio) {
    Q_UNUSED(o);
    QFileInfo fi(path);
    if (!fi.exists() || fi.size() < 100) return tr("the output file is missing or empty.");
    ProbeResult r = probeBlocking(path, 60000);
    if (!r.ok) return tr("the file cannot be read back (%1).").arg(r.error);
    if (expectVideo && !r.item.hasVideo) return tr("the video stream is missing.");
    if (expectAudio && !r.item.hasAudio) return tr("the audio stream is missing.");
    if (expected > 0) {
        const double tol = std::max(0.6, expected * 0.03);
        if (std::fabs(r.item.duration - expected) > tol)
            return tr("the duration is %1 s but %2 s was expected.").arg(QString::number(r.item.duration, 'f', 2), QString::number(expected, 'f', 2));
    }
    return {};
}

} // namespace mv
