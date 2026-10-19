// End-to-end tests of the editing engine: every test renders REAL media with FFmpeg and checks the result.
#include "AudioAnalysis.h"
#include "ExportJob.h"
#include "FfmpegTools.h"
#include "FontResolver.h"
#include "MediaProbe.h"
#include "RenderGraph.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <iostream>

using namespace mv;

static int g_pass = 0, g_fail = 0;
static QTemporaryDir g_dir;
static QString P(const QString &n) { return g_dir.filePath(n); }

#define CHECK(cond, msg)                                                                                   \
    do {                                                                                                   \
        if (cond) { ++g_pass; }                                                                            \
        else { ++g_fail; std::cout << "    FAIL: " << (msg).toStdString() << "   [" #cond "]" << std::endl; } \
    } while (0)

static void section(const char *n) { std::cout << "- " << n << std::endl; }

static bool run(const QStringList &args) {
    QProcess p; p.start(tools::ffmpegPath(), args); p.waitForFinished(120000);
    return p.exitCode() == 0;
}

static MediaItem media(const QString &path) {
    auto r = probeBlocking(path);
    if (!r.ok) { std::cout << "probe failed " << path.toStdString() << ": " << r.error.toStdString() << std::endl; }
    return r.item;
}

struct Result { bool ok = false; QString msg; ProbeResult out; double vdur = 0, adur = 0; };

static Result render(Project &p, ExportOptions o, bool cancelAfterMs = false, int cancelMs = 0) {
    Result r;
    ExportJob job;
    QEventLoop loop;
    QString err;
    bool done = false;
    QObject::connect(&job, &ExportJob::finished, &loop, [&](bool ok, const QString &msg, const QString &) { r.ok = ok; r.msg = msg; done = true; loop.quit(); });
    QObject::connect(&job, &ExportJob::logLine, [](const QString &l) { if (qEnvironmentVariableIsSet("MV_VERBOSE")) std::cout << "      " << l.left(400).toStdString() << std::endl; });
    if (!job.start(p, o, &err)) { r.msg = err; return r; }
    if (cancelAfterMs) QTimer::singleShot(cancelMs, &job, &ExportJob::cancel);
    if (!done) loop.exec();
    if (r.ok) {
        r.out = probeBlocking(o.outputPath);
        // per-stream durations for A/V sync checks
        QString j = tools::capture(tools::ffprobePath(), {"-v", "error", "-show_entries", "stream=codec_type,duration", "-of", "json", o.outputPath});
        auto arr = QJsonDocument::fromJson(j.toUtf8()).object()["streams"].toArray();
        for (auto v : arr) { auto s = v.toObject(); double d = s["duration"].toString().toDouble(); if (s["codec_type"] == "video") r.vdur = d; else r.adur = d; }
    }
    return r;
}

// Mean luma (0-255) of one frame at time t, optionally of a cropped region.
static double lumaAt(const QString &file, double t, const QString &crop = "iw:ih:0:0") {
    QProcess p; p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(tools::ffmpegPath(), {"-v", "error", "-ss", QString::number(t, 'f', 3), "-i", file, "-frames:v", "1", "-vf",
            QString("crop=%1,signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-").arg(crop), "-f", "null", "-"});
    p.waitForFinished(30000);
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    auto i = out.indexOf("YAVG=");
    return i < 0 ? -1 : out.mid(i + 5).section('\n', 0, 0).toDouble();
}
// Peak level (dBFS) of an audio window; very quiet/silent => -120.
static double peakDb(const QString &file, double from, double len) {
    QProcess p; p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(tools::ffmpegPath(), {"-hide_banner", "-ss", QString::number(from, 'f', 3), "-t", QString::number(len, 'f', 3), "-i", file, "-vn", "-af", "volumedetect", "-f", "null", "-"});
    p.waitForFinished(30000);
    const QString out = QString::fromUtf8(p.readAll());
    auto i = out.indexOf("max_volume:");
    if (i < 0) return -120;
    return out.mid(i + 11).section(" dB", 0, 0).trimmed().toDouble();
}

// Pitch of a pure tone, measured by counting zero crossings of the first channel of raw s16le audio.
static double toneHz(const QByteArray &pcm, int channels, int rate) {
    const qint16 *d = reinterpret_cast<const qint16 *>(pcm.constData());
    const int frames = pcm.size() / (2 * channels);
    int crossings = 0; int prev = 0;
    for (int i = 0; i < frames; ++i) { const int v = d[i * channels]; const int sgn = v > 300 ? 1 : v < -300 ? -1 : 0; if (sgn && prev && sgn != prev) ++crossings; if (sgn) prev = sgn; }
    return frames > 0 ? crossings / 2.0 / (double(frames) / rate) : 0;
}
static QByteArray runCapture(const QStringList &args) {
    QProcess p; p.start(tools::ffmpegPath(), args); p.waitForFinished(60000); return p.readAllStandardOutput();
}
static ExportOptions opts(const QString &fmt, const QString &file, int w = 0, int h = 0) {
    ExportOptions o; o.format = fmt; o.outputPath = P(file); o.width = w; o.height = h; o.quality = "draft"; o.x264Preset = "ultrafast";
    return o;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
static Clip clip(Project &p, const MediaItem &m, const QString &track, double start, double in, double out) {
    Clip c; c.id = newId("c"); c.mediaId = m.id; c.trackId = track; c.start = start; c.in = in; c.out = out;
    p.clips.push_back(c); return c;
}
static QString trackV1 = "V1", trackV2 = "V2", trackA = "A1", trackM = "M1", trackVO = "VO", trackT = "T1";
static Project newProject(const MediaItem &v) {
    Project p; p.ensureDefaultTracks(); p.media.push_back(v);
    const char *ids[] = {"T1", "V1", "V2", "A1", "M1", "VO"};
    for (int i = 0; i < 6; ++i) p.tracks[i].id = ids[i];
    p.settings.width = 640; p.settings.height = 360; p.settings.fps = 25;
    return p;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (!tools::available()) { std::cout << "ffmpeg not available" << std::endl; return 2; }
    std::cout << "Using " << tools::versionString().toStdString() << std::endl;

    // ----------------------------------------------------------------------------- test media
    section("generate source media");
    QString dirSp = P("folder with spaces");
    QDir().mkpath(dirSp);
    const QString arName = QString::fromUtf8("\xd9\x85\xd9\x82\xd8\xb7\xd8\xb9 \xd9\x81\xd9\x8a\xd8\xaf\xd9\x8a\xd9\x88 \xe2\x9c\x93.mp4"); // Arabic name + symbol
    bool gen = run({"-y", "-v", "error", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=25:d=6", "-f", "lavfi", "-i", "sine=f=440:r=44100:d=6",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-g", "25", "-c:a", "aac", "-shortest", P("v1.mp4")}) &&
               run({"-y", "-v", "error", "-f", "lavfi", "-i", "mandelbrot=s=320x240:r=30", "-f", "lavfi", "-i", "sine=f=880:r=48000", "-t", "5",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", P("v2.mp4")}) &&
               run({"-y", "-v", "error", "-f", "lavfi", "-i", "sine=f=330:r=44100:d=8", "-c:a", "libmp3lame", "-b:a", "128k", P("music.mp3")}) &&
               run({"-y", "-v", "error", "-f", "lavfi", "-i", "sine=f=500:r=44100:d=1", "-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono:d=2",
                    "-f", "lavfi", "-i", "sine=f=500:r=44100:d=1", "-filter_complex", "[0][1][2]concat=n=3:v=0:a=1", P("gap.wav")}) &&
               run({"-y", "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=400x300:d=1", "-frames:v", "1", P("img.png")}) &&
               run({"-y", "-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=100x100:d=1", "-frames:v", "1", P("logo.png")}) &&
               run({"-y", "-v", "error", "-f", "lavfi", "-i", "color=c=0x303030:s=640x360:r=25:d=4", "-c:v", "libx264", "-pix_fmt", "yuv420p", P("dark.mp4")}) &&
               run({"-y", "-v", "error", "-f", "lavfi", "-i", "color=c=0xd0d0d0:s=640x360:r=25:d=4", "-c:v", "libx264", "-pix_fmt", "yuv420p", P("light.mp4")}) &&
               QFile::copy(P("v1.mp4"), dirSp + "/" + arName);
    CHECK(gen, QString("could not generate test media"));
    if (!gen) return 1;
    MediaItem v1 = media(P("v1.mp4")), v2 = media(P("v2.mp4")), music = media(P("music.mp3")), gap = media(P("gap.wav")), img = media(P("img.png")), logo = media(P("logo.png"));
    CHECK(v1.kind == "video" && near(v1.duration, 6, 0.2) && v1.width == 640 && v1.hasAudio, QString("probe v1"));
    CHECK(music.kind == "audio" && near(music.duration, 8, 0.2) && music.sampleRate == 44100, QString("probe mp3"));
    CHECK(img.kind == "image", QString("probe image"));
    // ----------------------------------------------------------------------------- trim
    section("trim a video clip (re-encode)");
    {
        Project p = newProject(v1); clip(p, v1, trackV1, 0, 1, 3);
        auto r = render(p, opts("mp4-h264", "trim.mp4"));
        CHECK(r.ok, r.msg);
        CHECK(r.ok && near(r.out.item.duration, 2.0, 0.15) && r.out.item.hasVideo && r.out.item.hasAudio, QString("trim duration/streams"));
        CHECK(r.ok && near(r.vdur, r.adur, 0.15), QString("trim A/V sync"));
        CHECK(r.ok && r.out.item.width == 640 && r.out.item.height == 360, QString("trim size"));
        CHECK(!QFile::exists(P("trim.mveditor-part.mp4")), QString("no leftover part file"));
        CHECK(r.ok && lumaAt(P("trim.mp4"), 0.5) > 30, QString("trim video is not black (luma %1)").arg(lumaAt(P("trim.mp4"), 0.5)));
        CHECK(r.ok && peakDb(P("trim.mp4"), 0.2, 1.0) > -30, QString("trim audio is audible (%1 dB)").arg(peakDb(P("trim.mp4"), 0.2, 1.0)));
    }

    // ----------------------------------------------------------------------------- speed
    section("speed control changes real media duration");
    for (double sp : {0.5, 0.25, 2.0, 4.0, 8.0, 16.0}) {
        Project p = newProject(v1); auto c = clip(p, v1, trackV1, 0, 0, 4);
        p.clipById(c.id)->fx.speed = sp;
        const double expect = 4.0 / sp;
        CHECK(near(p.clipById(c.id)->duration(), expect, 1e-9), QString("model duration speed %1").arg(sp));
        auto r = render(p, opts("mp4-h264", QString("speed_%1.mp4").arg(sp)));
        CHECK(r.ok, QString("speed %1: %2").arg(sp).arg(r.msg));
        CHECK(r.ok && near(r.out.item.duration, expect, std::max(0.2, expect * 0.05)), QString("speed %1 expected %2 got %3").arg(sp).arg(expect).arg(r.ok ? r.out.item.duration : -1));
        CHECK(r.ok && r.out.item.hasAudio && near(r.vdur, r.adur, 0.25), QString("speed %1 A/V sync v=%2 a=%3").arg(sp).arg(r.vdur).arg(r.adur));
        CHECK(r.ok && lumaAt(P(QString("speed_%1.mp4").arg(sp)), 0.05) > 30 && peakDb(P(QString("speed_%1.mp4").arg(sp)), 0.0, 0.2) > -30, QString("speed %1 has picture and sound").arg(sp));
    }
    {   // 0.1x slow motion of a short slice, pitch not preserved
        Project p = newProject(v1); auto c = clip(p, v1, trackV1, 0, 0, 1);
        p.clipById(c.id)->fx.speed = 0.1; p.clipById(c.id)->fx.preservePitch = false;
        auto r = render(p, opts("mp4-h264", "speed_0.1.mp4"));
        CHECK(r.ok && near(r.out.item.duration, 10.0, 0.4), QString("0.1x slow motion %1").arg(r.msg));
    }
    CHECK(RenderGraph::atempoChain(16).count("atempo") == 4 && RenderGraph::atempoChain(0.1).count("atempo") == 4, QString("atempo chains: %1 | %2").arg(RenderGraph::atempoChain(16), RenderGraph::atempoChain(0.1)));
    CHECK(RenderGraph::atempoChain(1.0).isEmpty(), QString("atempo 1x empty"));

    // ----------------------------------------------------------------------------- concat / merge
    section("merge clips on a track");
    {
        Project p = newProject(v1); p.media.push_back(v2);
        clip(p, v1, trackV1, 0, 0, 2); clip(p, v2, trackV1, 2, 0, 2);
        auto r = render(p, opts("mp4-h264", "merge.mp4"));
        CHECK(r.ok && near(r.out.item.duration, 4.0, 0.2), QString("merge %1").arg(r.msg));
        CHECK(r.ok && near(r.vdur, r.adur, 0.2), QString("merge sync"));
    }

    // ----------------------------------------------------------------------------- transitions
    section("transitions: fade, dissolve, wipes");
    for (QString tr : {"fade", "dissolve", "wipeleft", "wiperight", "wipeup", "wipedown"}) {
        Project p = newProject(v1); p.media.push_back(v2);
        clip(p, v1, trackV1, 0, 0, 3); auto c2 = clip(p, v2, trackV1, 3, 0, 3);
        p.clipById(c2.id)->fx.transition = tr; p.clipById(c2.id)->fx.transitionDur = 1.0;
        auto r = render(p, opts("mp4-h264", "tr_" + tr + ".mp4"));
        CHECK(r.ok && near(r.out.item.duration, 6.0, 0.25), QString("transition %1: %2").arg(tr, r.msg));
    }
    {   // verify the transitions really blend/reveal, using flat dark/light clips so luma is predictable
        MediaItem dark = media(P("dark.mp4")), light = media(P("light.mp4"));
        auto mk = [&](const QString &tr) {
            Project p = newProject(dark); p.media.push_back(light);
            clip(p, dark, trackV1, 0, 0, 3); auto c2 = clip(p, light, trackV1, 3, 0, 3);
            p.clipById(c2.id)->fx.transition = tr; p.clipById(c2.id)->fx.transitionDur = 1.0;
            return p;
        };
        Project pd = mk("dissolve"); auto rd = render(pd, opts("mp4-h264", "dissolve_check.mp4"));
        const double a0 = lumaAt(P("dissolve_check.mp4"), 2.5), mid = lumaAt(P("dissolve_check.mp4"), 3.5), a1 = lumaAt(P("dissolve_check.mp4"), 5.0);
        CHECK(rd.ok && near(a0, 57, 8) && near(a1, 195, 8) && mid > 105 && mid < 150, QString("dissolve: before=%1 mid=%2 after=%3 (expect ~57, ~126, ~195)").arg(a0).arg(mid).arg(a1));
        Project pw = mk("wipeleft"); auto rw = render(pw, opts("mp4-h264", "wipe_check.mp4"));
        const double wl = lumaAt(P("wipe_check.mp4"), 3.5, "iw/5:ih:0:0"), wr = lumaAt(P("wipe_check.mp4"), 3.5, "iw/5:ih:iw*4/5:0");
        CHECK(rw.ok && near(wl, 195, 10) && near(wr, 57, 10), QString("wipeleft mid: left=%1 (new, ~208) right=%2 (old, ~48)").arg(wl).arg(wr));
        Project pr = mk("wiperight"); auto rr = render(pr, opts("mp4-h264", "wipe_check2.mp4"));
        const double rl = lumaAt(P("wipe_check2.mp4"), 3.5, "iw/5:ih:0:0"), rrr = lumaAt(P("wipe_check2.mp4"), 3.5, "iw/5:ih:iw*4/5:0");
        CHECK(rr.ok && near(rl, 57, 10) && near(rrr, 195, 10), QString("wiperight mid: left=%1 (old) right=%2 (new)").arg(rl).arg(rrr));
        Project pf = mk("fade"); auto rf = render(pf, opts("mp4-h264", "fade_check.mp4"));
        CHECK(rf.ok && lumaAt(P("fade_check.mp4"), 3.0) < 30 && lumaAt(P("fade_check.mp4"), 5.5) > 190, QString("fade (dip to black) works"));
    }

    // ----------------------------------------------------------------------------- video effects
    section("video effects: rotate/flip/crop/colour/sharpen/pip/opacity/reverse/image/watermark");
    {
        Project p = newProject(v1); auto c = clip(p, v1, trackV1, 0, 0, 2);
        auto &fx = p.clipById(c.id)->fx;
        fx.rotation = 90; fx.flipH = true; fx.flipV = true; fx.cropL = 0.1; fx.cropR = 0.1; fx.cropT = 0.05; fx.cropB = 0.05;
        fx.brightness = 0.1; fx.contrast = 1.2; fx.saturation = 1.3; fx.sharpness = 1.0; fx.balR = 0.2; fx.balB = -0.2;
        fx.fadeIn = 0.5; fx.fadeOut = 0.5;
        auto r = render(p, opts("mp4-h264", "fx.mp4"));
        CHECK(r.ok && near(r.out.item.duration, 2.0, 0.2), QString("effects chain %1").arg(r.msg));
    }
    {
        Project p = newProject(v1); p.media.push_back(v2); p.media.push_back(logo);
        clip(p, v1, trackV1, 0, 0, 3);
        auto c = clip(p, v2, trackV2, 0.5, 0, 2); auto &fx = p.clipById(c.id)->fx; fx.scale = 0.3; fx.posX = 0.95; fx.posY = 0.05; fx.opacity = 0.8; fx.fadeIn = 0.3;
        auto w = clip(p, logo, trackV2, 2.5, 0, 0.5); Q_UNUSED(w);
        auto r = render(p, opts("mp4-h264", "pip.mp4"));
        CHECK(r.ok && near(r.out.item.duration, 3.0, 0.2), QString("picture in picture + watermark %1").arg(r.msg));
    }
    {
        Project p = newProject(v1); auto c = clip(p, v1, trackV1, 0, 1, 3); p.clipById(c.id)->fx.reverse = true;
        auto r = render(p, opts("mp4-h264", "rev.mp4"));
        CHECK(r.ok && near(r.out.item.duration, 2.0, 0.2) && near(r.vdur, r.adur, 0.2), QString("reverse %1").arg(r.msg));
    }
    {
        Project p = newProject(img); p.settings.width = 640; p.settings.height = 360;
        clip(p, img, trackV1, 0, 0, 2); clip(p, img, trackV1, 2, 0, 1.5);
        auto r = render(p, opts("mp4-h264", "slides.mp4"));
        CHECK(r.ok && near(r.out.item.duration, 3.5, 0.25), QString("image slideshow %1").arg(r.msg));
    }

    // ----------------------------------------------------------------------------- layering / solo / hide
    section("track layering, solo and hide (picture)");
    {
        MediaItem dark = media(P("dark.mp4")), light = media(P("light.mp4"));
        auto mk = [&]() { Project p = newProject(light); p.media.push_back(dark); clip(p, light, trackV1, 0, 0, 2); clip(p, dark, trackV2, 0, 0, 2); return p; };
        Project a = mk();   // Video 2 is lower in the list than Video 1 => drawn on top => dark wins
        auto ra = render(a, opts("mp4-h264", "layer_top.mp4"));
        CHECK(ra.ok && near(lumaAt(P("layer_top.mp4"), 1.0), 57, 10), QString("later video track is on top (luma %1, expect ~57)").arg(lumaAt(P("layer_top.mp4"), 1.0)));
        Project b = mk(); b.trackById(trackV2)->hidden = true;
        auto rb = render(b, opts("mp4-h264", "layer_hidden.mp4"));
        CHECK(rb.ok && near(lumaAt(P("layer_hidden.mp4"), 1.0), 195, 10), QString("hidden top track reveals the one below (luma %1)").arg(lumaAt(P("layer_hidden.mp4"), 1.0)));
        Project c = mk(); c.trackById(trackV1)->solo = true;
        auto rc = render(c, opts("mp4-h264", "layer_solo.mp4"));
        CHECK(rc.ok && near(lumaAt(P("layer_solo.mp4"), 1.0), 195, 10), QString("soloing Video 1 shows only Video 1 (luma %1)").arg(lumaAt(P("layer_solo.mp4"), 1.0)));
        CHECK(Project().trackVisible(Track()) && Project().trackAudible(Track()), QString("default track is visible and audible"));
        Project d = mk(); d.trackById(trackA)->solo = true;      // soloing an AUDIO track must not blank the picture
        auto rd = render(d, opts("mp4-h264", "solo_audio_picture.mp4"));
        CHECK(rd.ok && near(lumaAt(P("solo_audio_picture.mp4"), 1.0), 57, 10), QString("soloing an audio track keeps the picture"));
    }

    section("speed change keeps the voice natural (pitch preserved) - measured");
    {
        // source: 330 Hz tone. At 2x speed the duration halves; with pitch preservation the tone must stay 330 Hz (not 660 Hz).
        Project p = newProject(music); auto c = clip(p, music, trackM, 0, 0, 6); p.clipById(c.id)->fx.speed = 2.0;
        auto r = render(p, opts("wav", "pitch_keep.wav"));
        const double keep = toneHz(runCapture({"-v", "error", "-i", P("pitch_keep.wav"), "-ac", "1", "-ar", "8000", "-f", "s16le", "-"}), 1, 8000);
        CHECK(r.ok && near(r.out.item.duration, 3.0, 0.2) && near(keep, 330, 15), QString("export at 2x keeps the pitch: %1 Hz (source 330 Hz)").arg(keep));
        Project q = newProject(music); auto c2 = clip(q, music, trackM, 0, 0, 6); q.clipById(c2.id)->fx.speed = 2.0; q.clipById(c2.id)->fx.preservePitch = false;
        auto r2 = render(q, opts("wav", "pitch_shift.wav"));
        const double shifted = toneHz(runCapture({"-v", "error", "-i", P("pitch_shift.wav"), "-ac", "1", "-ar", "8000", "-f", "s16le", "-"}), 1, 8000);
        CHECK(r2.ok && near(shifted, 660, 25), QString("without 'preserve pitch' the tone doubles: %1 Hz").arg(shifted));
        for (double sp : {0.5, 1.5, 4.0}) {
            const QByteArray pcm = runCapture(RenderGraph::liveAudioArgs(P("music.mp3"), 0.5, sp));
            const double hz = toneHz(pcm, 2, 48000);
            const double expectSec = (8.0 - 0.5) / sp;
            CHECK(near(hz, 330, 15) && near(pcm.size() / 4.0 / 48000.0, expectSec, 0.3), QString("live preview audio at %1x: %2 Hz, %3 s (expected 330 Hz, %4 s)").arg(sp).arg(hz).arg(pcm.size() / 4.0 / 48000.0).arg(expectSec));
        }
    }

    section("title fonts and effects (family/bold/italic, outline, shadow, caps, opacity)");
    {
        const QString f1 = resolveFontFile("DejaVu Sans", true, false), f2 = resolveFontFile("DejaVu Sans", false, true);
#ifndef Q_OS_WIN
        CHECK(!f1.isEmpty() && !f2.isEmpty() && f1 != f2, QString("font lookup finds different files for bold / italic: %1 | %2").arg(f1, f2));
#endif
        CHECK(resolveFontFile("", true, false).isEmpty(), QString("empty family means default font"));
        Project p = newProject(v1); clip(p, v1, trackV1, 0, 0, 2);
        Clip t; t.id = newId("c"); t.trackId = trackT; t.start = 0; t.in = 0; t.out = 2; t.title.text = "Styled title";
        t.title.fontFamily = "DejaVu Sans"; t.title.bold = true; t.title.italic = true; t.title.caps = true; t.title.textOpacity = 0.9;
        t.title.outline = true; t.title.outlineWidth = 4; t.title.outlineColor = "#ff0000"; t.title.shadow = true; t.title.shadowColor = "#0000ff";
        t.title.fontSize = 120; t.title.posY = 0.5; t.title.box = false; p.clips.push_back(t);
        auto r = render(p, opts("mp4-h264", "title_fx.mp4"));
        CHECK(r.ok && near(r.out.item.duration, 2.0, 0.2), QString("styled title renders: %1").arg(r.msg));
        Project q = newProject(v1); clip(q, v1, trackV1, 0, 0, 2);   // same without text: the picture must differ where the text is
        auto r2 = render(q, opts("mp4-h264", "title_none.mp4"));
        const double a = lumaAt(P("title_fx.mp4"), 1.0, "iw/2:ih/6:iw/4:ih*5/12"), b = lumaAt(P("title_none.mp4"), 1.0, "iw/2:ih/6:iw/4:ih*5/12");
        CHECK(r2.ok && std::fabs(a - b) > 1.0, QString("title text is visible in the picture (luma %1 vs %2)").arg(a).arg(b));
        QJsonObject j = t.toJson(); Clip back = Clip::fromJson(j);
        CHECK(back.title.italic && back.title.outline && back.title.caps && back.title.fontFamily == "DejaVu Sans" && back.title.shadow, QString("title style survives save/load"));
    }

    section("gaps: found before/between clips, closed one by one or all at once, other tracks untouched");
    {
        Project p = newProject(v1);
        clip(p, v1, trackV1, 1.0, 0, 2); clip(p, v1, trackV1, 5.0, 0, 2); clip(p, v1, trackV1, 7.0, 0, 1);
        auto other = clip(p, v1, trackV2, 10.0, 0, 1);
        const auto g = p.gapsOnTrack(trackV1);
        CHECK(g.size() == 2 && near(g[0].start, 0, 1e-9) && near(g[0].end, 1, 1e-9) && near(g[1].start, 3, 1e-9) && near(g[1].end, 5, 1e-9), QString("gaps before the first clip and between clips are found (no gap where clips touch)"));
        Project a = p; a.closeGap(trackV1, 3, 5);
        CHECK(near(a.clips[1].start, 3, 1e-9) && near(a.clips[2].start, 5, 1e-9) && near(a.clips[0].start, 1, 1e-9) && a.gapsOnTrack(trackV1).size() == 1, QString("closing one gap moves only the later clips back"));
        Project b = p; b.closeAllGaps(trackV1);
        CHECK(near(b.clips[0].start, 0, 1e-9) && near(b.clips[1].start, 2, 1e-9) && near(b.clips[2].start, 4, 1e-9) && b.gapsOnTrack(trackV1).isEmpty(), QString("close all gaps packs the clips of the track"));
        CHECK(near(b.clipById(other.id)->start, 10, 1e-9), QString("other tracks are not touched"));
    }

    section("markers: per track, time-ordered Ma/Mp naming, colours, old projects");
    {
        Project p = newProject(v1);
        Marker a; a.time = 3.0; a.trackId = trackV1; Marker b; b.time = 1.0; b.trackId = trackV1; Marker c; c.time = 2.0; c.trackId = trackV1; c.color = "#39ff14";
        Marker d; d.time = 5.0; d.trackId = trackA; Marker legacy; legacy.time = 9.0; legacy.name = "M7";          // no track: belongs to the first video track
        p.markers = {a, b, c, d, legacy};
        const auto v1idx = p.markerIndexesOnTrack(trackV1);
        CHECK(v1idx.size() == 4 && near(p.markers[v1idx[0]].time, 1.0, 1e-9) && near(p.markers[v1idx[3]].time, 9.0, 1e-9), QString("markers are ordered by time per track"));
        CHECK(p.markerLabel(v1idx[0]) == "Ma" && p.markerLabel(v1idx[1]) == "Mp" && p.markerLabel(v1idx[2]) == "Ma" && p.markerLabel(p.markerIndexesOnTrack(trackA).first()) == "Ma", QString("labels alternate Ma/Mp and restart on every track"));
        const QJsonObject j = p.toJson();
        QString err; Project q = Project::fromJson(j, "x.mveproj", &err);
        CHECK(q.markers.size() == 5 && q.markers[2].color == "#39ff14" && q.markers[3].trackId == trackA, QString("marker colour and track survive save/load"));
        CHECK(q.markers[4].name.isEmpty(), QString("old automatic names (M7) become Ma/Mp automatically"));
    }

    section("render signature ignores markers/names, reacts to real edits");
    {
        Project p = newProject(v1); auto c = clip(p, v1, trackV1, 0, 0, 2);
        const QByteArray sig = p.renderSignature();
        p.markers.push_back({1.0, "a", "#ff0000"}); p.tracks[0].name = "x"; p.tracks[0].height = 99; p.tracks[0].locked = true; p.media[0].name = "renamed.mp4";
        CHECK(p.renderSignature() == sig, QString("markers, names, heights, locks do not change the render signature"));
        p.clipById(c.id)->fx.brightness = 0.1;
        CHECK(p.renderSignature() != sig, QString("an effect changes the render signature"));
        Project q = newProject(v1); clip(q, v1, trackV1, 0, 0, 2); q.trackById(trackV1)->muted = true;
        CHECK(q.renderSignature() != sig, QString("muting a track changes the render signature"));
    }

    // ----------------------------------------------------------------------------- titles
    section("titles, captions (incl. special characters)");
    {
        Project p = newProject(v1); clip(p, v1, trackV1, 0, 0, 3);
        Clip t; t.id = newId("c"); t.trackId = trackT; t.start = 0.5; t.in = 0; t.out = 2;
        t.title.text = "Hello: it's 100% \"fun\"\nSecond line \\ backslash, comma; [brackets] %{pts}";
        t.fx.fadeIn = 0.3; t.fx.fadeOut = 0.3; p.clips.push_back(t);
        Clip t2 = t; t2.id = newId("c"); t2.start = 2.2; t2.out = 0.7; t2.title.text = QString::fromUtf8("\xd8\xb9\xd9\x86\xd9\x88\xd8\xa7\xd9\x86"); t2.title.posY = 0.1; p.clips.push_back(t2);
        auto r = render(p, opts("mp4-h264", "titles.mp4"));
        CHECK(r.ok && near(r.out.item.duration, 3.0, 0.2), QString("titles %1").arg(r.msg));
    }

    // ----------------------------------------------------------------------------- audio
    section("audio: mix, voice-over delay, music, mute/solo, effects");
    {
        Project p = newProject(v1); p.media.push_back(music); p.media.push_back(gap);
        clip(p, v1, trackV1, 0, 0, 4);
        auto m = clip(p, music, trackM, 0, 0, 6); p.clipById(m.id)->fx.volume = 0.4; p.clipById(m.id)->fx.fadeOut = 1.5;
        clip(p, gap, trackVO, 1.0, 0, 3);
        auto r = render(p, opts("mp3", "mix.mp3"));
        CHECK(r.ok && near(r.out.item.duration, 6.0, 0.25), QString("mix %1 dur %2").arg(r.msg).arg(r.ok ? r.out.item.duration : -1));
        // solo the voice track: silence of the other tracks
        p.trackById(trackVO)->solo = true;
        auto r2 = render(p, opts("wav", "solo.wav"));
        CHECK(r2.ok, QString("solo render %1").arg(r2.msg));
        CHECK(r2.ok && peakDb(P("solo.wav"), 1.2, 0.6) > -30 && peakDb(P("solo.wav"), 2.5, 1.0) < -50 && peakDb(P("solo.wav"), 0.1, 0.6) < -50,
              QString("solo voice-over only, delayed to 1s: %1 / %2 / %3 dB").arg(peakDb(P("solo.wav"), 1.2, 0.6)).arg(peakDb(P("solo.wav"), 2.5, 1.0)).arg(peakDb(P("solo.wav"), 0.1, 0.6)));
        CHECK(r.ok && peakDb(P("mix.mp3"), 0.1, 0.7) > -35, QString("mix contains music+video audio"));
        p.trackById(trackVO)->solo = false; p.trackById(trackM)->muted = true; p.trackById(trackVO)->muted = true;
        auto r3 = render(p, opts("wav", "muted.wav"));
        CHECK(r3.ok, QString("muted tracks render %1").arg(r3.msg));
        // only the video's own audio remains (0-4s); beyond that nothing
        CHECK(r3.ok && peakDb(P("muted.wav"), 0.2, 1.0) > -30 && peakDb(P("muted.wav"), 4.5, 1.0) < -50, QString("muted music/voice tracks are silent"));
    }
    {
        Project p = newProject(music); auto c = clip(p, music, trackM, 0, 1, 5);
        auto &fx = p.clipById(c.id)->fx;
        fx.normalize = true; fx.compressor = true; fx.denoise = true; fx.eqBass = 4; fx.eqMid = -2; fx.eqTreble = 3; fx.fadeIn = 0.5; fx.fadeOut = 0.5;
        auto o = opts("mp3", "fx.mp3"); o.sampleRate = 44100; o.channels = 1; o.audioBitrateKbps = 128;
        auto r = render(p, o);
        CHECK(r.ok && near(r.out.item.duration, 4.0, 0.25), QString("audio effects %1").arg(r.msg));
        CHECK(r.ok && r.out.item.channels == 1 && r.out.item.sampleRate == 44100, QString("mono/44.1k respected"));
    }
    {   // independent pitch change (+5 semitones) keeps duration
        Project p = newProject(music); auto c = clip(p, music, trackM, 0, 0, 4);
        p.clipById(c.id)->fx.pitchSemitones = 5;
        auto r = render(p, opts("wav", "pitch.wav"));
        CHECK(r.ok && near(r.out.item.duration, 4.0, 0.2), QString("pitch shift keeps duration %1 %2").arg(r.msg).arg(r.ok ? r.out.item.duration : -1));
        // speed + pitch not preserved: 2x faster and higher => 2 s
        Project q = newProject(music); auto c2 = clip(q, music, trackM, 0, 0, 4);
        q.clipById(c2.id)->fx.speed = 2; q.clipById(c2.id)->fx.preservePitch = false;
        auto r2 = render(q, opts("wav", "chipmunk.wav"));
        CHECK(r2.ok && near(r2.out.item.duration, 2.0, 0.2), QString("2x without pitch preservation %1").arg(r2.msg));
    }
    section("extract audio from video & convert formats");
    {
        Project p = Project::singleClip(v1, 0, 4);
        for (QString f : {"mp3", "wav", "aac", "flac"}) {
            auto o = opts(f, "extract." + formatById(f).ext);
            auto r = render(p, o);
            CHECK(r.ok && near(r.out.item.duration, 4.0, 0.25) && r.out.item.kind == "audio", QString("extract to %1: %2").arg(f, r.msg));
        }
    }

    // ----------------------------------------------------------------------------- video formats
    section("export formats and resolutions");
    for (QString f : {"mp4-h265", "webm", "mov"}) {
        Project p = newProject(v1); clip(p, v1, trackV1, 0, 0, 2);
        QString ext = formatById(f).ext;
        auto r = render(p, opts(f, "fmt_" + f + "." + ext, 320, 180));
        if (!r.ok && r.msg.contains("encoder")) { std::cout << "    (skipped " << f.toStdString() << ": " << r.msg.toStdString() << ")" << std::endl; continue; }
        CHECK(r.ok && near(r.out.item.duration, 2.0, 0.3) && r.out.item.width == 320, QString("format %1: %2").arg(f, r.msg));
    }
    {
        Project p = newProject(v1); clip(p, v1, trackV1, 0, 0, 2);
        auto o = opts("mp4-h264", "fps60.mp4", 854, 480); o.fps = 60; o.videoBitrateKbps = 1500; o.audioBitrateKbps = 96;
        auto r = render(p, o);
        CHECK(r.ok && r.out.item.height == 480 && near(r.out.item.fps, 60, 0.5), QString("480p/60fps/bitrate %1").arg(r.msg));
    }

    // ----------------------------------------------------------------------------- stream copy
    section("lossless stream copy");
    {
        Project p = Project::singleClip(v1, 1, 4);
        auto o = opts("mp4-h264", "copy.mp4"); o.streamCopy = true;
        CHECK(RenderGraph::reencodeReason(p, o).isEmpty(), QString("copy allowed: %1").arg(RenderGraph::reencodeReason(p, o)));
        auto r = render(p, o);
        CHECK(r.ok && near(r.out.item.duration, 3.0, 0.6) && r.out.item.videoCodec == "h264", QString("copy export %1").arg(r.msg));
        p.clips[0].fx.speed = 2;
        CHECK(!RenderGraph::reencodeReason(p, o).isEmpty(), QString("effects force re-encode"));
        Project pa = Project::singleClip(music, 2, 5);
        auto oa = opts("mp3", "copy.mp3"); oa.streamCopy = true;
        oa.sampleRate = 0; oa.channels = 0;
        CHECK(RenderGraph::reencodeReason(pa, oa).isEmpty(), QString("mp3 copy allowed: %1").arg(RenderGraph::reencodeReason(pa, oa)));
        auto ra = render(pa, oa);
        CHECK(ra.ok && near(ra.out.item.duration, 3.0, 0.3), QString("mp3 lossless trim %1").arg(ra.msg));
        auto ob = oa; ob.format = "wav";
        CHECK(!RenderGraph::reencodeReason(pa, ob).isEmpty(), QString("mp3->wav needs re-encode"));
    }

    // ----------------------------------------------------------------------------- safety
    section("safety: missing source, overwrite protection, unsupported input");
    {
        Project p = newProject(v1); clip(p, v1, trackV1, 0, 0, 2);
        auto o = opts("mp4-h264", "v1.mp4");   // would overwrite the source!
        QString err; ExportJob j;
        CHECK(!j.start(p, o, &err) && err.contains("source"), QString("refuses to overwrite source: %1").arg(err));
        CHECK(QFileInfo(P("v1.mp4")).size() == v1.sizeBytes, QString("source untouched"));
        p.media[0].path = P("does_not_exist.mp4");
        ExportJob j2; err.clear();
        CHECK(!j2.start(p, opts("mp4-h264", "x.mp4"), &err) && err.contains("missing"), QString("missing media reported: %1").arg(err));
        QFile bad(P("corrupt.mp4")); bad.open(QIODevice::WriteOnly); bad.write("this is not a video"); bad.close();
        auto pr = probeBlocking(P("corrupt.mp4"));
        CHECK(!pr.ok && !pr.error.isEmpty(), QString("corrupt file detected: %1").arg(pr.error));
        Project e; e.ensureDefaultTracks(); ExportJob j3; err.clear();
        CHECK(!j3.start(e, opts("mp4-h264", "empty.mp4"), &err), QString("empty timeline rejected: %1").arg(err));
        Project big = newProject(v1); auto c = clip(big, v1, trackV1, 0, 0, 5); big.clipById(c.id)->fx.reverse = true; big.clipById(c.id)->out = 200;
        CHECK(!RenderGraph::validate(big, opts("mp4-h264", "r.mp4")).isEmpty(), QString("huge reverse rejected"));
    }

    // ----------------------------------------------------------------------------- paths
    section("paths with spaces and non-English characters");
    {
        MediaItem ar = media(dirSp + "/" + arName);
        CHECK(ar.kind == "video", QString("probe Arabic filename"));
        Project p = newProject(ar); clip(p, ar, trackV1, 0, 0, 2);
        auto o = opts("mp4-h264", "x"); o.outputPath = dirSp + "/" + QString::fromUtf8("\xd9\x86\xd8\xaa\xd9\x8a\xd8\xac\xd8\xa9 \xd9\x86\xd9\x87\xd8\xa7\xd8\xa6\xd9\x8a\xd8\xa9.mp4");
        auto r = render(p, o);
        CHECK(r.ok && near(r.out.item.duration, 2.0, 0.2), QString("export to/from Arabic path: %1").arg(r.msg));
    }

    // ----------------------------------------------------------------------------- cancel
    section("cancel an export safely");
    {
        Project p = newProject(v1); auto c = clip(p, v1, trackV1, 0, 0, 6); p.clipById(c.id)->fx.reverse = true;
        for (int i = 1; i < 6; ++i) clip(p, v1, trackV1, i * 6, 0, 6);
        auto o = opts("mp4-h264", "cancel.mp4", 1280, 720); o.x264Preset = "veryslow"; o.quality = "max";
        auto r = render(p, o, true, 700);
        CHECK(!r.ok && r.msg.contains("cancel"), QString("cancel result: %1").arg(r.msg));
        CHECK(!QFile::exists(P("cancel.mp4")) && !QFile::exists(P("cancel.mveditor-part.mp4")), QString("no partial output after cancel"));
    }

    // ----------------------------------------------------------------------------- hardware fallback
    section("hardware encoder with automatic software fallback");
    {
        Project p = newProject(v1); clip(p, v1, trackV1, 0, 0, 2);
        auto o = opts("mp4-h264", "hw.mp4"); o.hwaccel = "auto";
        auto r = render(p, o);
        CHECK(r.ok && near(r.out.item.duration, 2.0, 0.2), QString("hw auto (falls back if unavailable): %1").arg(r.msg));
    }

    // ----------------------------------------------------------------------------- analysis
    section("silence detection / waveform / thumbnails");
    {
        const QString txt = "[silencedetect @ 0x1] silence_start: 1.0\n[silencedetect @ 0x1] silence_end: 3.0 | silence_duration: 2\n[silencedetect @ 0x1] silence_start: 5.5";
        auto sil = SilenceDetector::parseSilences(txt, 6.0);
        CHECK(sil.size() == 2 && near(sil[0].start, 1, 1e-6) && near(sil[1].end, 6, 1e-6), QString("parse silences"));
        auto keep = SilenceDetector::invert(sil, 6.0);
        CHECK(keep.size() == 2 && near(keep[0].start, 0, 1e-6) && near(keep[0].end, 1, 1e-6) && near(keep[1].start, 3, 1e-6) && near(keep[1].end, 5.5, 1e-6), QString("invert silences"));
        QEventLoop loop; QVector<TimeRange> found; bool ok = false;
        SilenceDetector det; QObject::connect(&det, &SilenceDetector::finished, &loop, [&](const QVector<TimeRange> &k, bool o) { found = k; ok = o; loop.quit(); });
        det.start(P("gap.wav"), -40, 0.5, gap.duration);
        QTimer::singleShot(30000, &loop, &QEventLoop::quit); loop.exec();
        CHECK(ok && found.size() == 2 && near(found[0].end, 1.0, 0.1) && near(found[1].start, 3.0, 0.1), QString("real silence detection found %1 segments").arg(found.size()));
        WaveformLoader wl; QVector<float> peaks; QEventLoop l2;
        QObject::connect(&wl, &WaveformLoader::ready, &l2, [&](const QString &, const QVector<float> &p) { peaks = p; l2.quit(); });
        QObject::connect(&wl, &WaveformLoader::failed, &l2, &QEventLoop::quit);
        wl.start(P("gap.wav")); QTimer::singleShot(30000, &l2, &QEventLoop::quit); l2.exec();
        CHECK(peaks.size() >= 395 && peaks.size() <= 405, QString("waveform has %1 peaks (expected ~400)").arg(peaks.size()));
        if (peaks.size() > 300) CHECK(peaks[50] > 0.08f && peaks[200] < 0.01f, QString("waveform shows tone and silence"));
        ThumbnailGenerator tg; QString th; QEventLoop l3;
        QObject::connect(&tg, &ThumbnailGenerator::ready, &l3, [&](const QString &, const QString &p) { th = p; l3.quit(); });
        tg.request("m1", P("v1.mp4"), 1.0, false); QTimer::singleShot(30000, &l3, &QEventLoop::quit); l3.exec();
        CHECK(!th.isEmpty() && QFileInfo(th).size() > 500, QString("thumbnail generated"));
    }

    // ----------------------------------------------------------------------------- project format
    section("project save/load, relative paths, overlap resolution");
    {
        Project p = newProject(v1); p.media.push_back(music);
        auto c = clip(p, v1, trackV1, 1.5, 0.5, 3.5); p.clipById(c.id)->fx.speed = 2; p.clipById(c.id)->fx.transition = "dissolve";
        Clip t; t.id = "title1"; t.trackId = trackT; t.start = 0; t.out = 2; t.title.text = "Hi"; p.clips.push_back(t);
        p.markers.push_back({2.0, "mark"});
        QJsonObject j = p.toJson(P("proj/test.mveproj"));
        QDir().mkpath(P("proj"));
        QByteArray bytes = QJsonDocument(j).toJson();
        QString err;
        Project q = Project::fromJson(QJsonDocument::fromJson(bytes).object(), P("proj/test.mveproj"), &err);
        CHECK(err.isEmpty() && q.clips.size() == 2 && q.media.size() == 2 && q.tracks.size() == 6, QString("roundtrip counts"));
        CHECK(q.clipById(c.id) && near(q.clipById(c.id)->fx.speed, 2, 1e-9) && q.clipById(c.id)->fx.transition == "dissolve" && near(q.clipById(c.id)->duration(), 1.5, 1e-9), QString("roundtrip clip props"));
        CHECK(q.markers.size() == 1 && q.clipById("title1")->title.text == "Hi", QString("roundtrip markers/titles"));
        // relink via relative path: move media next to the project
        QJsonObject moved = j; auto arr = moved["media"].toArray(); auto m0 = arr[0].toObject(); m0["path"] = "/nonexistent/v1.mp4"; m0["relPath"] = "../v1.mp4"; arr[0] = m0; moved["media"] = arr;
        Project r = Project::fromJson(moved, P("proj/test.mveproj"), &err);
        CHECK(!r.media[0].missing, QString("relative path relink"));
        auto m1 = arr[0].toObject(); m1["path"] = "/nonexistent/v1.mp4"; m1["relPath"] = ""; arr[0] = m1; moved["media"] = arr;
        Project s = Project::fromJson(moved, P("proj/test.mveproj"), &err);
        CHECK(s.media[0].missing, QString("missing media flagged"));
        Project bad = Project::fromJson(QJsonObject{{"format", "other"}}, "x", &err);
        CHECK(!err.isEmpty(), QString("foreign file rejected"));
        // overlap resolution
        Project o2 = newProject(v1); clip(o2, v1, trackV1, 0, 0, 2); clip(o2, v1, trackV1, 5, 0, 2);
        double s1 = o2.resolveOverlap(trackV1, 1.0, 2.0);
        CHECK(near(s1, 2.0, 1e-6) || near(s1, 3.0, 1e-6), QString("overlap pushed to free slot: %1").arg(s1));
        CHECK(near(o2.resolveOverlap(trackV1, 2.5, 2.0), 2.5, 1e-6), QString("free slot kept"));
    }

    std::cout << "\nResult: " << g_pass << " checks passed, " << g_fail << " failed" << std::endl;
    return g_fail ? 1 : 0;
}
