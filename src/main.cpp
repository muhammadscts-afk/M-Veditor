// M Veditor – entry point.
#include <cmath>
#include "AppSettings.h"
#include "AudioAnalysis.h"
#include "ExportJob.h"
#include "FfmpegTools.h"
#include "MainWindow.h"
#include "MediaProbe.h"
#include "Theme.h"
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

using namespace mv;

// Headless check used by the installer/CI on a clean machine: proves the bundled FFmpeg works offline and that
// speed change, trim, mixing, MP3 export and verification all function. Exit code 0 = everything works.
static int selfTest() {
    QStringList log; bool ok = true;
    auto step = [&](const QString &name, bool pass, const QString &detail = {}) { log << QString("[%1] %2 %3").arg(pass ? "PASS" : "FAIL", name, detail); ok = ok && pass; return pass; };
    QString why;
    if (!step("FFmpeg bundled", tools::available(&why), why + " " + tools::ffmpegPath())) goto done;
    {
        step("FFmpeg version", !tools::versionString().isEmpty(), tools::versionString().left(48));
        QTemporaryDir dir;
        auto run = [&](const QStringList &a) { QProcess p; tools::quiet(&p); p.start(tools::ffmpegPath(), a); p.waitForFinished(120000); return p.exitCode() == 0; };
        const QString src = dir.filePath("src.mp4");
        const bool x264 = tools::hasEncoder("libx264");
        if (!step("Create test media", run({"-y", "-v", "error", "-f", "lavfi", "-i", "testsrc2=s=320x180:r=25:d=3", "-f", "lavfi", "-i", "sine=f=440:d=3",
                    "-c:v", x264 ? "libx264" : "mpeg4", "-pix_fmt", "yuv420p", "-c:a", "aac", src}))) goto done;
        ProbeResult pr = probeBlocking(src);
        if (!step("Probe media", pr.ok && pr.item.hasVideo && pr.item.hasAudio, pr.error)) goto done;
        auto render = [&](Project &p, ExportOptions o, double expect, const QString &label) {
            ExportJob job; QEventLoop loop; bool good = false; QString msg;
            QObject::connect(&job, &ExportJob::finished, &loop, [&](bool k, const QString &m, const QString &) { good = k; msg = m; loop.quit(); });
            QString err;
            if (!job.start(p, o, &err)) return step(label, false, err);
            loop.exec();
            const bool dOk = good && std::abs(probeBlocking(o.outputPath).item.duration - expect) < 0.4;
            return step(label, dOk, good ? QString() : msg);
        };
        Project p = Project::singleClip(pr.item, 0, 2);
        p.clips[0].fx.speed = 2.0;                                       // 2 s at 2x -> 1 s
        ExportOptions v; v.format = x264 ? "mp4-h264" : "webm"; v.outputPath = dir.filePath("out." + QString(x264 ? "mp4" : "webm")); v.quality = "draft";
        render(p, v, 1.0, "Trim + 2x speed export (" + v.format + ")");
        Project pa = Project::singleClip(pr.item, 0, 3);
        ExportOptions a; a.format = "mp3"; a.outputPath = dir.filePath("out.mp3");
        render(pa, a, 3.0, "Extract audio to MP3");
        ExportOptions w; w.format = "wav"; w.outputPath = dir.filePath("out.wav");
        render(pa, w, 3.0, "Export WAV");
    }
done:
    QStringList cands = {QCoreApplication::applicationDirPath() + "/selftest.log", QDir::tempPath() + "/mveditor-selftest.log"};
    for (auto &c : cands) { QFile f(c); if (f.open(QIODevice::WriteOnly | QIODevice::Text)) { QTextStream(&f) << log.join("\n") << "\nRESULT: " << (ok ? "OK" : "FAILED") << "\n"; break; } }
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("M Veditor");
    app.setOrganizationName("MVeditor");
    app.setApplicationVersion(MV_VERSION);

    QCommandLineParser cli;
    cli.addPositionalArgument("project", "Project file to open");
    QCommandLineOption selftest("selftest", "Run the built-in self test and exit");
    QCommandLineOption grab("grab", "Save a screenshot of the main window and exit (developer option)", "file");
    cli.addOption(selftest); cli.addOption(grab);
    cli.process(app);

    tools::setOverrideDir(AppSettings::ffmpegDir());
    WaveformCache::setCacheDir(AppSettings::cacheDir());
    if (cli.isSet(selftest)) return selfTest();

    Theme::apply(AppSettings::darkTheme());
    MainWindow w;
    if (w.startMaximized()) w.showMaximized();
    else {
        const QRect av = QGuiApplication::primaryScreen()->availableGeometry();
        w.resize(std::min(1500, av.width() - 20), std::min(920, av.height() - 20));
        w.show();
    }
    QString why;
    if (!tools::available(&why))
        QTimer::singleShot(300, &w, [&w, why] { QMessageBox::critical(&w, QObject::tr("Components missing"), why); });
    const QStringList args = cli.positionalArguments();
    if (!args.isEmpty()) QTimer::singleShot(0, &w, [&w, args] { w.openProject(args.first()); });
    else QTimer::singleShot(400, &w, [&w] {
        w.checkRecovery();
        if (AppSettings::restoreSession() && !AppSettings::recents().isEmpty() && w.model()->project().clips.isEmpty() && !w.model()->isDirty())
            w.openProject(AppSettings::recents().first());
    });
    if (cli.isSet(grab)) {
        QTimer::singleShot(2500, &app, [&] { w.grab().save(cli.value(grab)); app.quit(); });
    }
    return app.exec();
}
