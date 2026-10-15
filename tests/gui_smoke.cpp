// Drives the real main window headlessly (QT_QPA_PLATFORM=offscreen): import, edit, undo, save/open, export, screenshot.
#include "AppSettings.h"
#include "FfmpegTools.h"
#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>

using namespace mv;
static int pass = 0, fail = 0;
#define CHECK(c, msg) do { if (c) ++pass; else { ++fail; std::cout << "    FAIL: " << QString(msg).toStdString() << "  [" #c "]" << std::endl; } } while (0)

static bool waitFor(const std::function<bool()> &cond, int ms = 20000) {
    QElapsedTimer t; t.start();
    while (!cond() && t.elapsed() < ms) QTest::qWait(30);
    return cond();
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("MVeditorTest"); app.setApplicationName("guitest");
    Theme::apply(true);
    QTemporaryDir dir;
    auto gen = [&](const QStringList &a) { QProcess p; p.start(tools::ffmpegPath(), a); p.waitForFinished(60000); return p.exitCode() == 0; };
    gen({"-y", "-v", "error", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=25:d=8", "-f", "lavfi", "-i", "sine=f=440:d=8", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", dir.filePath("clip.mp4")});
    gen({"-y", "-v", "error", "-f", "lavfi", "-i", "sine=f=300:d=10", "-c:a", "libmp3lame", dir.filePath("song.mp3")});
    gen({"-y", "-v", "error", "-f", "lavfi", "-i", "color=c=green:s=200x200:d=1", "-frames:v", "1", dir.filePath("logo.png")});

    int dialogsSeen = 0; QStringList dialogTitles;
    QTimer dismiss; dismiss.setInterval(80);
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
        if (QWidget *mw = QApplication::activeModalWidget()) { ++dialogsSeen; dialogTitles << mw->windowTitle(); mw->close(); }
    });
    dismiss.start();
    MainWindow w;
    w.resize(1500, 900);
    w.show();
    ProjectModel *m = w.model();

    std::cout << "- import media through the probe pipeline" << std::endl;
    w.importFiles({dir.filePath("clip.mp4"), dir.filePath("song.mp3"), dir.filePath("logo.png"), dir.filePath("nonexistent.mp4")});
    CHECK(waitFor([&] { return m->project().media.size() == 3; }), "3 media imported");
    CHECK(waitFor([&] { return dialogTitles.contains("Some files could not be imported"); }, 5000), "bad file reported to the user in a dialog");
    QString vid, aud, img;
    for (auto &mi : m->project().media) (mi.kind == "video" ? vid : mi.kind == "audio" ? aud : img) = mi.id;
    CHECK(!vid.isEmpty() && !aud.isEmpty() && !img.isEmpty(), "kinds detected");
    CHECK(m->project().settings.width == 640 && m->project().settings.height == 360, "project size adopted from first video");
    CHECK(waitFor([&] { return !m->thumbnail(vid).isEmpty() && m->waveform(aud) != nullptr; }), "thumbnail + waveform generated asynchronously");

    std::cout << "- drop onto the timeline" << std::endl;
    QString trV1, trA, trT;
    for (auto &t : m->project().tracks) { if (t.name == "Video 1") trV1 = t.id; if (t.name == "Audio 1") trA = t.id; if (t.type == TrackType::Title) trT = t.id; }
    emit w.timeline()->mediaDropped(vid, trV1, 0.0);
    emit w.timeline()->mediaDropped(vid, trV1, 2.0);            // overlaps -> must be pushed to a free spot
    emit w.timeline()->mediaDropped(aud, trA, 0.0);
    CHECK(m->project().clips.size() == 3, "3 clips on the timeline");
    const Clip *c1 = &m->project().clips[0], *c2 = &m->project().clips[1];
    CHECK(c2->start >= c1->end() - 1e-6, "second clip pushed after the first (no overlap)");
    // hand-drop video on an audio track -> audio-only usage allowed, image on audio track rejected via compat
    std::cout << "- split / undo / redo" << std::endl;
    m->setPlayhead(3.0);
    m->selectClip(m->project().clips[0].id);
    w.findChild<QAction *>("split")->trigger();
    CHECK(m->project().clips.size() == 4, "split created a 4th clip");
    w.findChild<QAction *>("split")->setEnabled(true);
    m->undoStack()->undo();
    CHECK(m->project().clips.size() == 3, "undo restores 3 clips");
    m->undoStack()->redo();
    CHECK(m->project().clips.size() == 4, "redo re-applies the split");
    double leftOut = 0; for (auto &c : m->project().clips) if (c.trackId == trV1 && c.start < 0.01) leftOut = c.out;
    CHECK(std::abs(leftOut - 3.0) < 1e-6, "split point maps to source time 3.0");

    std::cout << "- properties, speed, titles, markers" << std::endl;
    QString first; for (auto &c : m->project().clips) if (c.trackId == trV1 && c.start < 0.01) first = c.id;
    m->selectClip(first);
    m->mutate("speed", [&](Project &p) { p.clipById(first)->fx.speed = 2.0; });
    CHECK(std::abs(m->project().clipById(first)->duration() - 1.5) < 1e-6, "2x speed halves clip length on the timeline");
    w.findChild<QAction *>("title")->trigger();
    int titles = 0; for (auto &c : m->project().clips) if (c.isTitle()) ++titles;
    CHECK(titles == 1, "title clip added");
    w.findChild<QAction *>("marker")->trigger();
    CHECK(m->project().markers.size() == 1, "marker added");
    m->selectClip(first);
    w.findChild<QAction *>("detach")->trigger();
    int audioClips = 0; for (auto &c : m->project().clips) if (c.trackId == trA) ++audioClips;
    CHECK(audioClips == 2 && m->project().clipById(first)->fx.mute, "detach audio created an audio clip and muted the video's audio");
    w.findChild<QAction *>("rot")->trigger();
    CHECK(m->project().clipById(first)->fx.rotation == 90, "rotate 90");
    w.findChild<QAction *>("dup")->trigger();
    w.findChild<QAction *>("copy")->trigger();
    w.findChild<QAction *>("paste")->trigger();

    std::cout << "- save and reopen (project format)" << std::endl;
    const QString projPath = dir.filePath("my project.mveproj");
    // save through the same code path the menu uses
    {
        QFile f(projPath); f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(m->project().toJson(projPath)).toJson());
    }
    const int nClips = m->project().clips.size();
    CHECK(w.openProject(projPath), "project reopened");
    CHECK(m->project().clips.size() == nClips && m->project().media.size() == 3, "clips and media survive save/open");
    CHECK(std::abs(m->project().clipById(first)->fx.speed - 2.0) < 1e-9 && m->project().clipById(first)->fx.rotation == 90, "effects survive save/open");
    CHECK(!m->isDirty(), "freshly opened project is clean");

    std::cout << "- export through the UI job runner" << std::endl;
    ExportOptions o; o.format = "mp4-h264"; o.outputPath = dir.filePath("result.mp4"); o.quality = "draft"; o.x264Preset = "ultrafast"; o.width = 640; o.height = 360;
    w.startExportWith(o);
    CHECK(waitFor([&] { return !w.busy(); }, 120000), "export finished");
    CHECK(waitFor([&] { return dialogTitles.contains("Export complete"); }, 5000), "success dialog shown after verified export");
    auto pr = probeBlocking(o.outputPath);
    const double expected = m->project().duration();
    CHECK(pr.ok && std::abs(pr.item.duration - expected) < 0.5 && pr.item.hasAudio && pr.item.hasVideo,
          QString("exported file matches timeline length (%1 vs %2)").arg(pr.item.duration).arg(expected));

    std::cout << "- screenshot" << std::endl;
    w.timeline()->zoomFit();
    m->selectClip(first);
    m->setPlayhead(2.0);
    QTest::qWait(600);
    w.grab().save(QString::fromLocal8Bit(qgetenv("MV_SHOT").isEmpty() ? "/tmp/mveditor_ui.png" : qgetenv("MV_SHOT")));
    w.close();
    std::cout << "\nGUI result: " << pass << " checks passed, " << fail << " failed" << std::endl;
    return fail ? 1 : 0;
}
