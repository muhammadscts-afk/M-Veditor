#include <functional>
#include "RenderGraph.h"
#include "FfmpegTools.h"
#include "FontResolver.h"
#include <QColor>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace mv {

QVector<FormatInfo> exportFormats() {
    return {
        {"mp4-h264", "MP4 (H.264)", "mp4", "mp4", true},
        {"mp4-h265", "MP4 (H.265 / HEVC)", "mp4", "mp4", true},
        {"webm", "WebM (VP9 + Opus)", "webm", "webm", true},
        {"mov", "MOV (H.264 + AAC)", "mov", "mov", true},
        {"mp3", "MP3 audio", "mp3", "mp3", false},
        {"wav", "WAV audio", "wav", "wav", false},
        {"aac", "AAC audio (.m4a)", "m4a", "ipod", false},
        {"flac", "FLAC audio", "flac", "flac", false},
    };
}
FormatInfo formatById(const QString &id) {
    for (auto &f : exportFormats()) if (f.id == id) return f;
    return exportFormats().first();
}

namespace {

QString num(double v) {
    QString s = QString::number(v, 'f', 6);
    if (s.contains('.')) { while (s.endsWith('0')) s.chop(1); if (s.endsWith('.')) s.chop(1); }
    if (s == "-0") s = "0";
    return s;
}
int even(double v) { int i = int(std::lround(v)); i &= ~1; return std::max(2, i); }

// A color for drawtext: 0xRRGGBB[@alpha]
QString fcolor(const QString &c, double alpha = 1.0) {
    QColor col(c);
    if (!col.isValid()) col = Qt::white;
    QString s = QString("0x%1").arg(col.rgb() & 0xffffff, 6, 16, QChar('0'));
    if (alpha < 0.999) s += "@" + num(alpha);
    return s;
}
// Filter option value for a path that contains ':' (e.g. C:/Windows/Fonts/arial.ttf)
QString fpath(QString p) {
    p.replace('\\', '/');
    p.replace(':', "\\:");
    return "'" + p + "'";
}

bool isVideoFormat(const QString &id) { return formatById(id).video; }

struct AudioSrc { int input; const Clip *clip; const Track *track; const MediaItem *media; };


const Clip *nextOnTrack(const Project &p, const Clip &c) {
    const Clip *best = nullptr;
    for (auto *x : p.clipsOnTrack(c.trackId)) if (x->start > c.start + 1e-6 && (!best || x->start < best->start)) best = x;
    return best;
}
bool isOverlayTransition(const QString &t) { return t == "dissolve" || t.startsWith("wipe"); }

} // namespace

QString RenderGraph::atempoChain(double t) {
    QStringList parts;
    while (t > 2.0 + 1e-9) { parts << "atempo=2.0"; t /= 2.0; }
    while (t < 0.5 - 1e-9) { parts << "atempo=0.5"; t /= 0.5; }
    if (std::fabs(t - 1.0) > 1e-9) parts << "atempo=" + num(t);
    return parts.join(',');
}

QString RenderGraph::defaultFontFile() {
    const QString env = qEnvironmentVariable("MVEDITOR_FONT");
    if (!env.isEmpty()) return env;
#ifdef Q_OS_WIN
    return "C:/Windows/Fonts/arialbd.ttf";
#elif defined(Q_OS_MAC)
    return "/System/Library/Fonts/Helvetica.ttc";
#else
    return "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
#endif
}

double RenderGraph::outputDuration(const Project &p, const ExportOptions &o) {
    double total = p.duration();
    if (o.rangeEnd > o.rangeStart) total = std::min(total, o.rangeEnd) - o.rangeStart;
    else if (o.rangeStart > 0) total -= o.rangeStart;
    return std::max(0.0, total);
}

QStringList RenderGraph::validate(const Project &p, const ExportOptions &o) {
    QStringList e;
    if (p.clips.isEmpty() || p.duration() <= 0) e << QObject::tr("The timeline is empty. Add some clips first.");
    bool titles = false;
    for (auto &c : p.clips) {
        if (c.isTitle()) { titles = true; continue; }
        const MediaItem *m = p.mediaById(c.mediaId);
        if (!m) { e << QObject::tr("A clip refers to media that is no longer in the project."); continue; }
        if (!QFileInfo::exists(m->path)) e << QObject::tr("Source file is missing: %1 (use File > Relink Missing Media).").arg(m->name);
        if (c.out <= c.in) e << QObject::tr("Clip from \"%1\" has no duration.").arg(m->name);
        if (c.fx.reverse && (c.out - c.in) > 180)
            e << QObject::tr("Reversing needs the whole clip in memory. Split \"%1\" into pieces of 3 minutes or less first.").arg(m->name);
        if (c.fx.speed < 0.1 - 1e-9 || c.fx.speed > 100) e << QObject::tr("Speed must be between 0.1x and 100x.");
    }
    const bool video = isVideoFormat(o.format);
    if (titles && video && !tools::hasFilter("drawtext"))
        e << QObject::tr("This FFmpeg build cannot draw text, so titles can't be rendered.");
    if (video) {
        int w = o.width ? o.width : p.settings.width, h = o.height ? o.height : p.settings.height;
        if (w < 16 || h < 16 || w > 8192 || h > 8192) e << QObject::tr("The output resolution is not valid.");
    }
    if (o.outputPath.isEmpty()) e << QObject::tr("Choose an output file.");
    if (!tools::available()) e << QObject::tr("FFmpeg was not found.");
    // never write over a source file
    const QString out = QFileInfo(o.outputPath).absoluteFilePath();
    for (auto &m : p.media)
        if (!out.isEmpty() && QFileInfo(m.path).absoluteFilePath().compare(out, Qt::CaseInsensitive) == 0)
            e << QObject::tr("The output file is also a source file used by the project. Choose a different name so your original is not overwritten.");
    return e;
}

QString RenderGraph::reencodeReason(const Project &p, const ExportOptions &o) {
    QStringList why;
    const bool videoOut = isVideoFormat(o.format);
    if (p.clips.size() != 1) return QObject::tr("Re-encoding is required: stream copy only works for a single, unmodified clip.");
    const Clip &c = p.clips.first();
    if (c.isTitle()) return QObject::tr("Re-encoding is required: titles are drawn onto the video.");
    const MediaItem *m = p.mediaById(c.mediaId);
    if (!m) return QObject::tr("Re-encoding is required.");
    if (m->kind == "image") return QObject::tr("Re-encoding is required for still images.");
    if (!c.fx.isDefaultForCopy()) return QObject::tr("Re-encoding is required: the clip has speed, effects, fades or volume changes.");
    if (o.width && m->width && o.width != m->width) why << QObject::tr("a different resolution");
    if (o.height && m->height && o.height != m->height) why << QObject::tr("a different resolution");
    if (o.fps > 0 && m->fps > 0 && std::fabs(o.fps - m->fps) > 0.01) why << QObject::tr("a different frame rate");
    if (!why.isEmpty()) return QObject::tr("Re-encoding is required for %1.").arg(why.join(", "));
    const QString vc = m->videoCodec, ac = m->audioCodec;
    auto in = [](const QString &x, const QStringList &l) { return l.contains(x); };
    if (videoOut) {
        if (!m->hasVideo) return QObject::tr("Re-encoding is required: the file has no video.");
        bool ok = false;
        if (o.format == "mp4-h264") ok = vc == "h264" && (ac.isEmpty() || in(ac, {"aac", "mp3", "ac3"}));
        else if (o.format == "mp4-h265") ok = vc == "hevc" && (ac.isEmpty() || in(ac, {"aac", "mp3", "ac3"}));
        else if (o.format == "mov") ok = in(vc, {"h264", "hevc", "prores"}) && (ac.isEmpty() || in(ac, {"aac", "mp3", "pcm_s16le", "alac"}));
        else if (o.format == "webm") ok = in(vc, {"vp8", "vp9", "av1"}) && (ac.isEmpty() || in(ac, {"opus", "vorbis"}));
        if (!ok) return QObject::tr("Re-encoding is required: the source codecs (%1/%2) don't fit the chosen format.").arg(vc, ac.isEmpty() ? "-" : ac);
    } else {
        if (!m->hasAudio) return QObject::tr("Re-encoding is required: the file has no audio.");
        bool ok = (o.format == "mp3" && ac == "mp3") || (o.format == "flac" && ac == "flac") ||
                  (o.format == "aac" && ac == "aac") || (o.format == "wav" && ac.startsWith("pcm_"));
        if (!ok) return QObject::tr("Re-encoding is required: converting %1 to this format.").arg(ac);
        if (o.sampleRate && m->sampleRate && o.sampleRate != m->sampleRate) return QObject::tr("Re-encoding is required for a different sample rate.");
        if (o.channels && m->channels && o.channels != m->channels) return QObject::tr("Re-encoding is required for a different channel count.");
    }
    return {};
}

RenderPlan RenderGraph::build(const Project &p, const ExportOptions &o, bool allowHardware) {
    RenderPlan plan;
    plan.errors = validate(p, o);
    if (!plan.errors.isEmpty()) return plan;

    const bool videoOut = isVideoFormat(o.format);
    const FormatInfo fmt = formatById(o.format);
    plan.duration = outputDuration(p, o);
    plan.hasVideo = videoOut;
    const QString ff = tools::ffmpegPath();
    Q_UNUSED(ff);

    // stdin stays open on purpose: writing "q" to it is how a running export is stopped safely.
    QStringList a = {"-hide_banner", "-y", "-loglevel", "error", "-progress", "pipe:1", "-nostats"};

    // ------------------------------------------------------------------ stream copy
    if (o.streamCopy) {
        const QString why = reencodeReason(p, o);
        if (!why.isEmpty()) { plan.errors << why; return plan; }
        const Clip &c = p.clips.first();
        const MediaItem *m = p.mediaById(c.mediaId);
        a << "-ss" << num(c.in) << "-t" << num(c.out - c.in) << "-i" << QFileInfo(m->path).absoluteFilePath();
        if (videoOut) a << "-map" << "0:v:0" << "-map" << "0:a:0?"; else a << "-vn" << "-map" << "0:a:0";
        a << "-c" << "copy" << "-avoid_negative_ts" << "make_zero";
        if (fmt.container == "mp4" || fmt.container == "mov" || fmt.container == "ipod") a << "-movflags" << "+faststart";
        a << "-f" << fmt.container << o.partPath;
        plan.args = a; plan.ok = true; plan.streamCopy = true; plan.duration = c.out - c.in;
        plan.hasAudio = m->hasAudio; plan.encoderName = "copy";
        return plan;
    }

    // ------------------------------------------------------------------ canvas
    const int W = even(o.width ? o.width : p.settings.width);
    const int H = even(o.height ? o.height : p.settings.height);
    const double F = o.fps > 0 ? o.fps : p.settings.fps;
    const int SR = o.sampleRate > 0 ? o.sampleRate : 48000;
    const QString lay = o.channels == 1 ? "mono" : "stereo";
    const double total = p.duration();   // timeline length; range is applied on the output

    QStringList fc;                       // filter graph lines
    int inputCount = 0, lbl = 0;
    auto nl = [&](const QString &pre) { return pre + QString::number(lbl++); };
    auto addInput = [&](const QStringList &pre, const MediaItem &m) {
        a << pre << "-i" << QFileInfo(m.path).absoluteFilePath();
        return inputCount++;
    };

    // ------------------------------------------------------------------ video
    if (videoOut) {
        QString base = nl("b");
        fc << QString("color=c=black:s=%1x%2:r=%3:d=%4[%5]").arg(W).arg(H).arg(num(F), num(total + 1), base);
        int bottomIdx = -1;
        for (int i = 0; i < p.tracks.size(); ++i) if (p.tracks[i].type == TrackType::Video) { bottomIdx = i; break; }   // lowest layer = first video track
        int titleN = 0;
        // Layer order: video tracks from the first to the last (later = on top), then title tracks on top of everything.
        for (int pass = 0; pass < 2; ++pass) for (int ti = 0; ti < p.tracks.size(); ++ti) {
            const Track &tr = p.tracks[ti];
            if (!p.trackVisible(tr)) continue;
            if (pass == 0 && tr.type != TrackType::Video) continue;
            if (pass == 1 && tr.type != TrackType::Title) continue;
            const bool bottom = (ti == bottomIdx);
            for (const Clip *cp : p.clipsOnTrack(tr.id)) {
                const Clip &c = *cp;
                const double dur = c.duration();
                const double s0 = c.start;
                // ---------------- titles
                if (c.isTitle()) {
                    const QString fn = QString("title_%1.txt").arg(titleN++);
                    plan.files.push_back({fn, (c.title.caps ? c.title.text.toUpper() : c.title.text).toUtf8()});
                    const double fs = std::max(8.0, c.title.fontSize * H / 1080.0);
                    const int bw = c.title.box ? int(fs * 0.3) : 0;
                    QString dt = QString("drawtext=textfile=%1:expansion=none:fontsize=%2:fontcolor=%3")
                                     .arg(fn, num(fs), fcolor(c.title.color, c.title.textOpacity));
                    QString font = o.fontFile;
                    if (font.isEmpty()) font = resolveFontFile(c.title.fontFamily, c.title.bold, c.title.italic);
                    if (font.isEmpty()) font = defaultFontFile();
                    if (QFileInfo::exists(font)) dt += ":fontfile=" + fpath(font);
                    const double sc = H / 1080.0;
                    if (c.title.outline) dt += QString(":borderw=%1:bordercolor=%2").arg(std::max<long>(1, std::lround(c.title.outlineWidth * sc))).arg(fcolor(c.title.outlineColor));
                    if (c.title.shadow) dt += QString(":shadowx=%1:shadowy=%2:shadowcolor=%3").arg(std::lround(c.title.shadowX * sc)).arg(std::lround(c.title.shadowY * sc)).arg(fcolor(c.title.shadowColor, c.title.shadowOpacity));
                    dt += QString(":x=(w-text_w-%1)*%2+%3:y=(h-text_h-%1)*%4+%5")
                              .arg(2 * bw).arg(num(c.title.posX)).arg(bw).arg(num(c.title.posY)).arg(bw);
                    if (c.title.box) dt += QString(":box=1:boxcolor=%1:boxborderw=%2").arg(fcolor(c.title.boxColor, c.title.boxOpacity)).arg(bw);
                    QStringList alpha;
                    if (c.fx.fadeIn > 0) alpha << QString("min(1,(t-%1)/%2)").arg(num(s0), num(c.fx.fadeIn));
                    if (c.fx.fadeOut > 0) alpha << QString("min(1,(%1-t)/%2)").arg(num(s0 + dur), num(c.fx.fadeOut));
                    if (!alpha.isEmpty()) dt += ":alpha='" + alpha.join("*") + "'";
                    dt += QString(":enable='between(t,%1,%2)'").arg(num(s0), num(s0 + dur));
                    const QString nb = nl("b");
                    fc << QString("[%1]%2[%3]").arg(base, dt, nb);
                    base = nb;
                    continue;
                }
                const MediaItem *m = p.mediaById(c.mediaId);
                if (!m || !m->hasVideo) continue;
                const ClipEffects &fx = c.fx;
                // ---------------- media input
                int idx;
                if (m->kind == "image")
                    idx = addInput({"-loop", "1", "-framerate", num(F), "-t", num(dur)}, *m);
                else
                    idx = addInput({"-ss", num(c.in), "-t", num(c.out - c.in)}, *m);
                QStringList f;
                f << "setpts=PTS-STARTPTS";
                if (m->kind == "image") f << "format=yuva420p";
                if (fx.cropL + fx.cropR + fx.cropT + fx.cropB > 1e-6)
                    f << QString("crop=trunc(iw*(1-%1-%2)/2)*2:trunc(ih*(1-%3-%4)/2)*2:trunc(iw*%1/2)*2:trunc(ih*%3/2)*2")
                             .arg(num(fx.cropL), num(fx.cropR), num(fx.cropT), num(fx.cropB));
                if (fx.rotation == 90) f << "transpose=1";
                else if (fx.rotation == 180) f << "transpose=1,transpose=1";
                else if (fx.rotation == 270) f << "transpose=2";
                if (fx.flipH) f << "hflip";
                if (fx.flipV) f << "vflip";
                if (fx.reverse && m->kind != "image") f << "reverse";
                if (std::fabs(fx.speed - 1) > 1e-9 && m->kind != "image") f << "setpts=PTS/" + num(fx.speed);
                f << "fps=" + num(F);
                if (std::fabs(fx.brightness) > 1e-6 || std::fabs(fx.contrast - 1) > 1e-6 || std::fabs(fx.saturation - 1) > 1e-6)
                    f << QString("eq=brightness=%1:contrast=%2:saturation=%3").arg(num(fx.brightness), num(fx.contrast), num(fx.saturation));
                if (std::fabs(fx.sharpness) > 1e-6) f << "unsharp=5:5:" + num(fx.sharpness) + ":5:5:0";
                if (std::fabs(fx.balR) + std::fabs(fx.balG) + std::fabs(fx.balB) > 1e-6)
                    f << QString("colorbalance=rm=%1:gm=%2:bm=%3").arg(num(fx.balR), num(fx.balG), num(fx.balB));
                f << QString("scale=%1:%2:force_original_aspect_ratio=decrease:flags=bicubic").arg(even(W * fx.scale)).arg(even(H * fx.scale));
                f << "scale=trunc(iw/2)*2:trunc(ih/2)*2" << "setsar=1";

                const double td = std::min(fx.transitionDur, dur);
                const bool trAlpha = isOverlayTransition(fx.transition) && td > 0;
                const bool fadeAlpha = !bottom && (fx.fadeIn > 0 || fx.fadeOut > 0);
                const bool needAlpha = m->kind == "image" || fx.opacity < 0.999 || trAlpha || fadeAlpha;
                if (needAlpha) f << "format=yuva420p";
                if (fx.opacity < 0.999) f << "colorchannelmixer=aa=" + num(fx.opacity);
                const QString alphaFlag = needAlpha ? ":alpha=1" : "";
                if (fx.transition == "fade" && td > 0) f << "fade=t=in:st=0:d=" + num(td) + (needAlpha && !bottom ? alphaFlag : "");
                if (fx.transition == "dissolve" && td > 0) f << "fade=t=in:st=0:d=" + num(td) + ":alpha=1";
                if (fx.fadeIn > 0) f << "fade=t=in:st=0:d=" + num(std::min(fx.fadeIn, dur)) + (fadeAlpha ? ":alpha=1" : "");
                double fo = fx.fadeOut;
                const Clip *nx = nextOnTrack(p, c);
                double padExtra = 0;
                if (nx && std::fabs(nx->start - c.end()) < 1.0 / std::max(F, 1.0) + 1e-3 && nx->fx.transitionDur > 0) {
                    if (nx->fx.transition == "fade") fo = std::max(fo, std::min(nx->fx.transitionDur, dur));
                    else if (isOverlayTransition(nx->fx.transition)) padExtra = std::min(nx->fx.transitionDur, nx->duration());
                }
                if (fo > 0) f << QString("fade=t=out:st=%1:d=%2%3").arg(num(std::max(0.0, dur - fo)), num(std::min(fo, dur)), fadeAlpha ? ":alpha=1" : "");
                if (padExtra > 0) f << "tpad=stop_mode=clone:stop_duration=" + num(padExtra);

                const double endT = s0 + dur + padExtra;
                const QString posX = QString("(main_w-overlay_w)*%1").arg(num(fx.posX));
                const QString posY = QString("(main_h-overlay_h)*%1").arg(num(fx.posY));
                auto overlay = [&](const QString &src, double from, double to) {
                    const QString nb = nl("b");
                    fc << QString("[%1][%2]overlay=x=%3:y=%4:eof_action=pass:enable='between(t,%5,%6)'[%7]")
                              .arg(base, src, posX, posY, num(from), num(to + 0.0001), nb);
                    base = nb;
                };
                if (trAlpha && fx.transition.startsWith("wipe") && td > 0) {
                    // Two segments: the wipe segment (geq, only first td seconds) + the untouched remainder.
                    const QString pre = nl("v"), a1 = nl("v"), a2 = nl("v"), a1b = nl("v"), a2b = nl("v");
                    fc << QString("[%1:v]%2[%3]").arg(idx).arg(f.join(','), pre);
                    fc << QString("[%1]split[%2][%3]").arg(pre, a1, a2);
                    QString cond;
                    const QString T = num(td);
                    if (fx.transition == "wipeleft") cond = QString("lt(X,W*min(1,T/%1))").arg(T);
                    else if (fx.transition == "wiperight") cond = QString("gt(X,W-W*min(1,T/%1))").arg(T);
                    else if (fx.transition == "wipeup") cond = QString("lt(Y,H*min(1,T/%1))").arg(T);
                    else cond = QString("gt(Y,H-H*min(1,T/%1))").arg(T);
                    fc << QString("[%1]trim=duration=%2,setpts=PTS-STARTPTS,format=yuva420p,geq=lum='lum(X,Y)':cb='cb(X,Y)':cr='cr(X,Y)':a='if(%3,255,0)',setpts=PTS+%4/TB[%5]")
                              .arg(a1, T, cond, num(s0), a1b);
                    fc << QString("[%1]trim=start=%2,setpts=PTS-STARTPTS+%3/TB[%4]").arg(a2, T, num(s0 + td), a2b);
                    overlay(a1b, s0, s0 + td);
                    overlay(a2b, s0 + td, endT);
                } else {
                    const QString vl = nl("v");
                    fc << QString("[%1:v]%2,setpts=PTS+%3/TB[%4]").arg(idx).arg(f.join(','), num(s0), vl);
                    overlay(vl, s0, endT);
                }
            }
        }
        fc << QString("[%1]format=yuv420p[vout]").arg(base);
    }

    // ------------------------------------------------------------------ audio
    QStringList mixIn;
    for (const Track &tr : p.tracks) {
        if (tr.type == TrackType::Title || !p.trackAudible(tr)) continue;
        for (const Clip *cp : p.clipsOnTrack(tr.id)) {
            const Clip &c = *cp;
            const MediaItem *m = p.mediaById(c.mediaId);
            if (!m || !m->hasAudio || c.fx.mute || m->kind == "image") continue;
            const ClipEffects &fx = c.fx;
            const double dur = c.duration();
            const int idx = addInput({"-ss", num(c.in), "-t", num(c.out - c.in)}, *m);
            QStringList f;
            f << QString("aresample=%1").arg(SR) << "asetpts=PTS-STARTPTS";
            if (fx.reverse) f << "areverse";
            const double pitchRate = std::pow(2.0, fx.pitchSemitones / 12.0);
            const double rate = (fx.preservePitch ? 1.0 : fx.speed) * pitchRate;
            if (std::fabs(rate - 1) > 1e-9) f << QString("asetrate=%1").arg(std::lround(SR * rate)) << QString("aresample=%1").arg(SR);
            const double tempo = fx.speed / rate;
            const QString at = atempoChain(tempo);
            if (!at.isEmpty()) f << at;
            f << QString("aformat=sample_fmts=fltp:channel_layouts=%1").arg(lay);
            const double vol = fx.volume * tr.volume;
            if (std::fabs(vol - 1) > 1e-6) f << "volume=" + num(vol);
            if (std::fabs(fx.eqBass) > 1e-6) f << QString("equalizer=f=100:t=h:w=100:g=%1").arg(num(fx.eqBass));
            if (std::fabs(fx.eqMid) > 1e-6) f << QString("equalizer=f=1000:t=q:w=1:g=%1").arg(num(fx.eqMid));
            if (std::fabs(fx.eqTreble) > 1e-6) f << QString("equalizer=f=8000:t=h:w=4000:g=%1").arg(num(fx.eqTreble));
            if (fx.denoise) f << "afftdn=nr=12:nf=-40";
            if (fx.compressor) f << "acompressor=threshold=0.125:ratio=3:attack=20:release=250:makeup=2";
            if (fx.normalize) f << "loudnorm=I=-16:TP=-1.5:LRA=11" << QString("aresample=%1").arg(SR)
                               << QString("aformat=sample_fmts=fltp:channel_layouts=%1").arg(lay);
            if (fx.fadeIn > 0) f << "afade=t=in:st=0:d=" + num(std::min(fx.fadeIn, dur));
            if (fx.fadeOut > 0) f << QString("afade=t=out:st=%1:d=%2").arg(num(std::max(0.0, dur - fx.fadeOut)), num(std::min(fx.fadeOut, dur)));
            const long delayMs = std::lround(c.start * 1000.0);
            if (delayMs > 0) f << QString("adelay=delays=%1:all=1").arg(delayMs);
            const QString al = nl("a");
            fc << QString("[%1:a]%2[%3]").arg(idx).arg(f.join(','), al);
            mixIn << al;
        }
    }
    {
        const QString silent = nl("a");
        fc << QString("anullsrc=r=%1:cl=%2,atrim=duration=%3[%4]").arg(SR).arg(lay).arg(num(total + 1), silent);
        mixIn.prepend(silent);
        const int n = mixIn.size();
        QString mix;
        for (auto &l : mixIn) mix += "[" + l + "]";
        mix += QString("amix=inputs=%1:duration=longest:dropout_transition=0").arg(n);
        if (tools::amixHasNormalize()) mix += ":normalize=0";
        else mix += QString(",volume=%1").arg(n);
        mix += QString(",aformat=sample_fmts=fltp:sample_rates=%1:channel_layouts=%2,atrim=0:%3,asetpts=PTS-STARTPTS")
                   .arg(SR).arg(lay).arg(num(total));
        fc << mix + "[aout]";
        plan.hasAudio = true;
    }

    // ------------------------------------------------------------------ filter graph delivery
    const QString graph = fc.join(";\n");
    if (graph.size() > 20000) {
        plan.files.push_back({"filter_graph.txt", graph.toUtf8()});
        if (tools::majorVersion() >= 7 || tools::majorVersion() == 0) a << "-/filter_complex" << "filter_graph.txt";
        else a << "-filter_complex_script" << "filter_graph.txt";
    } else {
        a << "-filter_complex" << graph;
    }

    // ------------------------------------------------------------------ encoders
    auto crfFor = [&](const QString &codec) {
        static const int x264[] = {28, 23, 19, 15};
        int qi = o.quality == "draft" ? 0 : o.quality == "high" ? 2 : o.quality == "max" ? 3 : 1;
        if (codec == "x265") return x264[qi] + 4;
        if (codec == "vp9") { static const int v[] = {38, 32, 27, 20}; return v[qi]; }
        return x264[qi];
    };
    const QString preset = o.preview ? "ultrafast" : o.x264Preset;
    if (videoOut) {
        a << "-map" << "[vout]" << "-map" << "[aout]";
        auto sw264 = [&]() {
            if (!tools::hasEncoder("libx264")) {
                if (tools::hasEncoder("h264_mf")) { plan.encoderName = "h264_mf"; return QStringList{"-c:v", "h264_mf", "-b:v", QString::number(o.videoBitrateKbps ? o.videoBitrateKbps : std::max(1500, W * H / 400)) + "k"}; }
                plan.errors << QObject::tr("This build has no H.264 encoder (the LGPL FFmpeg flavor). Choose a hardware encoder, WebM, or install the GPL flavor.");
                return QStringList{};
            }
            plan.encoderName = "libx264";
            QStringList r = {"-c:v", "libx264", "-preset", preset};
            if (o.videoBitrateKbps > 0) r << "-b:v" << QString::number(o.videoBitrateKbps) + "k";
            else r << "-crf" << QString::number(crfFor("x264"));
            return r;
        };
        auto hwArgs = [&](const QString &codec) -> QStringList {   // codec: h264 | hevc
            QStringList order;
            if (o.hwaccel == "auto") order = {"nvenc", "qsv", "amf"};
            else if (o.hwaccel != "none") order = {o.hwaccel};
            const int cq = crfFor("x264") + (codec == "hevc" ? 2 : 0);
            for (auto &kind : order) {
                const QString enc = codec + "_" + kind;
                if (!tools::hasEncoder(enc)) continue;
                plan.usedHardware = true; plan.encoderName = enc;
                QStringList r = {"-c:v", enc};
                if (kind == "nvenc") { r << "-preset" << "p5"; if (o.videoBitrateKbps > 0) r << "-b:v" << QString::number(o.videoBitrateKbps) + "k"; else r << "-rc" << "vbr" << "-cq" << QString::number(cq) << "-b:v" << "0"; }
                else if (kind == "qsv") { if (o.videoBitrateKbps > 0) r << "-b:v" << QString::number(o.videoBitrateKbps) + "k"; else r << "-global_quality" << QString::number(cq); }
                else { if (o.videoBitrateKbps > 0) r << "-b:v" << QString::number(o.videoBitrateKbps) + "k"; else r << "-rc" << "cqp" << "-qp_i" << QString::number(cq) << "-qp_p" << QString::number(cq); }
                return r;
            }
            return {};
        };
        QStringList venc;
        if (o.format == "mp4-h264" || o.format == "mov") {
            if (allowHardware && o.hwaccel != "none") venc = hwArgs("h264");
            if (venc.isEmpty()) venc = sw264();
            a << venc << "-pix_fmt" << "yuv420p" << "-c:a" << "aac" << "-b:a" << QString::number(o.audioBitrateKbps) + "k";
            if (fmt.container == "mp4") a << "-movflags" << "+faststart";
        } else if (o.format == "mp4-h265") {
            if (allowHardware && o.hwaccel != "none") venc = hwArgs("hevc");
            if (venc.isEmpty()) {
                if (!tools::hasEncoder("libx265")) plan.errors << QObject::tr("This build has no H.265 software encoder. Choose a hardware encoder or another format.");
                else {
                    plan.encoderName = "libx265";
                    venc = {"-c:v", "libx265", "-preset", preset, "-tag:v", "hvc1", "-x265-params", "log-level=error"};
                    if (o.videoBitrateKbps > 0) venc << "-b:v" << QString::number(o.videoBitrateKbps) + "k";
                    else venc << "-crf" << QString::number(crfFor("x265"));
                }
            } else venc << "-tag:v" << "hvc1";
            a << venc << "-pix_fmt" << "yuv420p" << "-c:a" << "aac" << "-b:a" << QString::number(o.audioBitrateKbps) + "k" << "-movflags" << "+faststart";
        } else if (o.format == "webm") {
            if (!tools::hasEncoder("libvpx-vp9")) plan.errors << QObject::tr("This build has no VP9 encoder.");
            plan.encoderName = "libvpx-vp9";
            a << "-c:v" << "libvpx-vp9" << "-row-mt" << "1" << "-deadline" << (o.preview ? "realtime" : "good") << "-cpu-used" << (o.preview ? "8" : "2");
            if (o.videoBitrateKbps > 0) a << "-b:v" << QString::number(o.videoBitrateKbps) + "k";
            else a << "-crf" << QString::number(crfFor("vp9")) << "-b:v" << "0";
            a << "-pix_fmt" << "yuv420p" << "-c:a" << (tools::hasEncoder("libopus") ? "libopus" : "libvorbis") << "-b:a" << QString::number(o.audioBitrateKbps) + "k";
        }
        a << "-r" << num(F);
    } else {
        a << "-map" << "[aout]" << "-vn";
        if (o.format == "mp3") { if (!tools::hasEncoder("libmp3lame")) plan.errors << QObject::tr("This build has no MP3 encoder."); a << "-c:a" << "libmp3lame" << "-b:a" << QString::number(o.audioBitrateKbps) + "k"; }
        else if (o.format == "wav") a << "-c:a" << "pcm_s16le";
        else if (o.format == "aac") a << "-c:a" << "aac" << "-b:a" << QString::number(o.audioBitrateKbps) + "k" << "-movflags" << "+faststart";
        else if (o.format == "flac") a << "-c:a" << "flac";
        plan.encoderName = a.at(a.indexOf("-c:a") + 1);
    }
    a << "-ar" << QString::number(SR) << "-ac" << QString::number(o.channels == 1 ? 1 : 2);
    if (o.rangeStart > 0) a << "-ss" << num(o.rangeStart);
    a << "-t" << num(plan.duration);
    a << "-f" << fmt.container << o.partPath;
    plan.args = a;
    plan.ok = plan.errors.isEmpty();
    return plan;
}

} // namespace mv
