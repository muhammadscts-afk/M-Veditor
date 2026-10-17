// Translates a Project into an FFmpeg command line (filter graph + encoder settings).
// This is the heart of the "real media" editing: speed, trim, effects and mixing are
// all executed on the actual media by FFmpeg, never just simulated in the preview.
#pragma once
#include "Project.h"
#include <QPair>
#include <QStringList>

namespace mv {

struct FormatInfo {
    QString id, label, ext, container;
    bool video;
};
QVector<FormatInfo> exportFormats();
FormatInfo formatById(const QString &id);

struct ExportOptions {
    QString format = "mp4-h264";   // mp4-h264 | mp4-h265 | webm | mov | mp3 | wav | aac | flac
    int width = 0, height = 0;     // 0 = project size
    double fps = 0;                // 0 = project fps
    QString quality = "standard";  // draft | standard | high | max
    int videoBitrateKbps = 0;      // 0 = quality-based (CRF)
    int audioBitrateKbps = 192;
    int sampleRate = 48000;
    int channels = 2;
    QString hwaccel = "none";      // none | auto | nvenc | qsv | amf
    QString x264Preset = "medium";
    bool streamCopy = false;       // lossless trim/convert without re-encoding (when possible)
    bool preview = false;          // fast low-res render used for timeline preview
    double rangeStart = 0, rangeEnd = 0; // optional range of the timeline (0/0 = everything)
    QString outputPath;            // final path
    QString partPath;              // temporary output written by FFmpeg (set by the caller)
    QString fontFile;              // optional override for titles
};

struct RenderPlan {
    bool ok = false;
    QStringList errors, warnings;
    QStringList args;                               // arguments for ffmpeg (no program name)
    QVector<QPair<QString, QByteArray>> files;      // helper files to write in the working dir
    double duration = 0;                            // expected output duration (s)
    bool hasVideo = false, hasAudio = false;
    bool usedHardware = false;
    bool streamCopy = false;
    QString encoderName;
};

class RenderGraph {
public:
    // Builds the plan. Relative helper files (title text) live in the process working directory.
    static RenderPlan build(const Project &p, const ExportOptions &o, bool allowHardware = true);
    static QStringList validate(const Project &p, const ExportOptions &o);
    // Empty string = a lossless stream copy is possible; otherwise explains why re-encoding is needed.
    static QString reencodeReason(const Project &p, const ExportOptions &o);
    static double outputDuration(const Project &p, const ExportOptions &o);
    static QString defaultFontFile();
    static QString atempoChain(double tempo);
    // ffmpeg arguments that stream stereo 48 kHz s16le audio from `startSec` at `speed` with the pitch preserved (live preview).
    static QStringList liveAudioArgs(const QString &path, double startSec, double speed);
};

} // namespace mv
