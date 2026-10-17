// Drives the real main window headlessly (QT_QPA_PLATFORM=offscreen): import, edit, undo, save/open, export, screenshot.
#include "AppSettings.h"
#include "FfmpegTools.h"
#include "MainWindow.h"
#include "PreviewMonitor.h"
#include "PropertiesPanel.h"
#include "Theme.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QMenu>
#include <QMenuBar>
#include <QTest>
#include <QDockWidget>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QToolButton>
#include <QToolTip>
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

    std::cout << "- track order, Add track button" << std::endl;
    {
        QStringList names; for (auto &t : m->project().tracks) names << t.name;
        CHECK(names.indexOf("Video 1") >= 0 && names.indexOf("Video 1") < names.indexOf("Video 2"), "Video 1 is listed above Video 2 (ascending)");
        QToolButton *addBtn = nullptr;
        for (auto *b : w.timeline()->viewport()->findChildren<QToolButton *>()) if (b->text().contains("Add track")) addBtn = b;
        CHECK(addBtn && addBtn->menu() && addBtn->menu()->actions().size() == 5, "Add track button with a 5-entry drop-down menu");
        const int before = m->project().tracks.size();
        if (addBtn && addBtn->menu()) addBtn->menu()->actions().first()->trigger();
        CHECK(m->project().tracks.size() == before + 1, "Add track > Video track adds a track");
        int v3 = -1, v2 = -1; for (int i = 0; i < m->project().tracks.size(); ++i) { if (m->project().tracks[i].name == "Video 3") v3 = i; if (m->project().tracks[i].name == "Video 2") v2 = i; }
        CHECK(v3 == v2 + 1, "new video track is numbered 3 and placed under Video 2");
        // reorder: move the new track to the top via the same mutation the drag/menu uses, then undo
        const QString firstId = m->project().tracks[0].id;
        m->mutate("Move track", [](Project &p) { p.tracks.swapItemsAt(0, 1); });
        CHECK(m->project().tracks[1].id == firstId, "tracks can be reordered");
        m->undoStack()->undo();
        CHECK(m->project().tracks[0].id == firstId, "track reorder is undoable");
        m->undoStack()->undo();   // remove the added track again
        CHECK(m->project().tracks.size() == before, "undo removes the added track");
    }
    std::cout << "- multi-selection" << std::endl;
    {
        QStringList v1clips; for (auto &c : m->project().clips) if (c.trackId == trV1) v1clips << c.id;
        CHECK(v1clips.size() >= 2, "there are at least two segments on Video 1");
        m->setSelection(v1clips.mid(0, 2));
        CHECK(m->selectedClips().size() == 2 && m->isSelected(v1clips[0]) && m->isSelected(v1clips[1]), "two segments are selected at once");
        m->toggleClip(v1clips[1]);
        CHECK(m->selectedClips().size() == 1, "Ctrl-click toggles a segment out of the selection");
        m->addToSelection(v1clips[1]);
        const int n = m->project().clips.size();
        w.findChild<QAction *>("delete")->trigger();
        CHECK(m->project().clips.size() == n - 2, "Delete removes all selected segments");
        m->undoStack()->undo();
        CHECK(m->project().clips.size() == n, "undo restores both");
        m->setSelection(v1clips.mid(0, 2));
        w.findChild<QAction *>("dup")->trigger();
        CHECK(m->project().clips.size() == n + 2 && m->selectedClips().size() == 2, "Duplicate copies every selected segment");
        m->undoStack()->undo();
        w.findChild<QAction *>("selectall")->trigger();
        CHECK(m->selectedClips().size() == m->project().clips.size(), "Select all");
        m->selectClip({});
    }
    std::cout << "- seek bar click, rendered preview play, stale preview" << std::endl;
    {
        auto *prev = w.findChild<PreviewMonitor *>();
        auto *slider = prev ? prev->findChild<SeekSlider *>() : nullptr;
        CHECK(slider != nullptr, "seek slider present");
        if (slider) {
            m->setPlayhead(0);
            const double dur = m->project().duration();
            QTest::mouseClick(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() / 2, slider->height() / 2));
            CHECK(std::abs(m->playhead() - dur / 2) < dur * 0.08, QString("one click on the middle of the seek bar jumps there (%1 of %2)").arg(m->playhead()).arg(dur));
            QTest::mouseClick(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() - 12, slider->height() / 2));
            CHECK(m->playhead() > dur * 0.85, "click near the end jumps near the end");
            QTest::mouseClick(slider, Qt::LeftButton, Qt::NoModifier, QPoint(12, slider->height() / 2));
            CHECK(m->playhead() < dur * 0.1, "click near the start jumps near the start");
        }
        QToolButton *liveBtn = nullptr;
        if (prev) for (auto *b : prev->findChildren<QToolButton *>()) if (b->text() == "Live") liveBtn = b;
        CHECK(liveBtn && liveBtn->isChecked(), "Live preview is ON by default (no rendering needed to play)");
        if (liveBtn) liveBtn->setChecked(false);                 // exact-preview flow below
        if (prev) {
            m->setPlayhead(0.5);
            prev->togglePlay();       // Live off: renders a full preview, then plays it
            CHECK(waitFor([&] { return prev->mode() == PreviewMonitor::Mode::Rendered; }, 120000), "Play renders the full preview and switches to the rendered player");
            QTest::qWait(300);
            m->mutate("marker during preview", [&](Project &p) { p.markers.push_back({1.0, "x"}); });
            CHECK(prev->mode() == PreviewMonitor::Mode::Rendered && !w.busy(), "adding a marker does NOT invalidate the preview (no re-render)");
            m->mutate("rename track during preview", [&](Project &p) { p.tracks[0].name = "renamed"; p.tracks[0].height = 80; });
            CHECK(prev->mode() == PreviewMonitor::Mode::Rendered, "track names/heights do not invalidate the preview either");
            QString firstClip = m->project().clips.first().id;
            m->mutate("edit during preview", [&](Project &p) { p.clipById(firstClip)->fx.brightness = 0.2; });
            CHECK(prev->mode() == PreviewMonitor::Mode::Timeline, "a real edit (brightness) makes the rendered preview stale (back to timeline mode)");
            m->undoStack()->undo(); m->undoStack()->undo(); m->undoStack()->undo();
        }
    }
    std::cout << "- live playback: no rendering, playhead advances smoothly, panels start narrow" << std::endl;
    {
        auto *prev = w.findChild<PreviewMonitor *>();
        QToolButton *lb = nullptr; for (auto *b : prev->findChildren<QToolButton *>()) if (b->text() == "Live") lb = b;
        if (lb) lb->setChecked(true);
        prev->pause();
        m->setPlayhead(1.0);
        QTest::qWait(200);
        prev->togglePlay();
        CHECK(prev->isPlaying() && prev->mode() == PreviewMonitor::Mode::Timeline && !w.busy(), "Space starts live playback instantly without any render job");
        QVector<double> samples;
        for (int i = 0; i < 12; ++i) { QTest::qWait(50); samples << m->playhead(); }
        prev->pause();
        bool monotonic = true; for (int i = 1; i < samples.size(); ++i) if (samples[i] < samples[i - 1] - 1e-9) monotonic = false;
        int distinct = 1; for (int i = 1; i < samples.size(); ++i) if (samples[i] > samples[i - 1] + 1e-6) ++distinct;
        CHECK(monotonic && samples.last() > 1.3 && samples.last() < 2.6, QString("playhead advances in real time (1.0 -> %1 s in ~0.6 s)").arg(samples.last()));
        CHECK(distinct >= 8, QString("playhead moves on almost every 50 ms sample (%1 of 12 distinct) = smooth").arg(distinct));
        m->mutate("marker while idle", [](Project &p) { p.markers.push_back({0.2, "z"}); });
        m->undoStack()->undo();
        auto *dp = w.findChild<QDockWidget *>("dockProps"); auto *dl = w.findChild<QDockWidget *>("dockLib");
        if (qEnvironmentVariableIsSet("MV_DEBUG_W")) for (const char *nm : {"dockProps", "dockExport"}) {
            auto *d = w.findChild<QDockWidget *>(nm); if (!d) continue;
            std::cout << nm << " minSizeHint=" << d->minimumSizeHint().width() << " widget=" << d->widget()->minimumSizeHint().width() << std::endl;
            for (auto *c : d->widget()->findChildren<QWidget *>()) if (c->minimumSizeHint().width() > 140 && c->isVisibleTo(d->widget()))
                std::cout << "   " << c->metaObject()->className() << " '" << c->objectName().toStdString() << "' min=" << c->minimumSizeHint().width() << " text=" << (qobject_cast<QLabel*>(c) ? qobject_cast<QLabel*>(c)->text().left(30).toStdString() : std::string()) << std::endl;
        }
        CHECK(dp && dl && dp->width() <= 260 && dl->width() <= 230, QString("docks start at (near) their minimum width: props %1, library %2").arg(dp ? dp->width() : -1).arg(dl ? dl->width() : -1));
    }
    std::cout << "- direct play of a single clip (no render)" << std::endl;
    {
        auto *prev = w.findChild<PreviewMonitor *>();
        const QString keepPath = m->filePath();
        Project saved = m->project();
        MediaItem mv0; for (auto &mi : saved.media) if (mi.kind == "video") mv0 = mi;
        Project single = Project::singleClip(mv0, 1.0, 5.0);
        single.tracks.clear(); single.ensureDefaultTracks();
        QString v1id; for (auto &t : single.tracks) if (t.name == "Video 1") v1id = t.id;
        single.clips[0].trackId = v1id;
        m->reset(single, QString());
        QToolButton *liveBtn2 = nullptr;
        for (auto *b : prev->findChildren<QToolButton *>()) if (b->text() == "Live") liveBtn2 = b;
        if (liveBtn2) liveBtn2->setChecked(false);
        QElapsedTimer tm; tm.start();
        prev->togglePlay();
        CHECK(prev->mode() == PreviewMonitor::Mode::Rendered && !w.busy() && tm.elapsed() < 3000, QString("single clip plays directly without rendering (%1 ms)").arg(tm.elapsed()));
        m->mutate("m", [](Project &p) { p.markers.push_back({0.5, "q"}); });
        CHECK(prev->mode() == PreviewMonitor::Mode::Rendered, "marker during direct play keeps playing");
        m->mutate("fx", [](Project &p) { p.clips[0].fx.saturation = 1.5; });
        CHECK(prev->mode() == PreviewMonitor::Mode::Timeline, "an effect on the clip leaves direct mode");
        prev->pause();
        if (liveBtn2) liveBtn2->setChecked(true);
        m->reset(saved, keepPath);
    }
    std::cout << "- marker colour regions" << std::endl;
    m->mutate("m1", [](Project &p) { p.markers.push_back({2.0, "A"}); p.markers.push_back({5.0, "B"}); });
    CHECK(TimelineView::regionColor(m->project(), 0) != TimelineView::regionColor(m->project(), 1), "regions have different colours");
    CHECK(TimelineView::regionColor(m->project(), 0).alpha() < 60, "regions are very transparent");
    QTest::qWait(200);
    w.grab().save("/tmp/mveditor_regions.png");
    m->undoStack()->undo();

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

    std::cout << "- Properties panel fits at minimum width (nothing cut off)" << std::endl;
    {
        w.resize(1366, 768);
        w.resizeDocks({w.findChild<QDockWidget *>("dockLib"), w.findChild<QDockWidget *>("dockProps")}, {1, 1}, Qt::Horizontal);
        QTest::qWait(300);
        auto *pp = w.findChild<PropertiesPanel *>(); auto *sa = pp->findChild<QScrollArea *>();
        auto check = [&](const QString &what) {
            QTest::qWait(250);
            const int vw = sa->viewport()->width();
            QStringList bad;
            for (QWidget *c : sa->widget()->findChildren<QWidget *>()) {
                if (!c->isVisibleTo(sa->widget())) continue;
                const QString cls = c->metaObject()->className();
                if (!(c->inherits("QAbstractSpinBox") || c->inherits("QComboBox") || c->inherits("QPushButton") || c->inherits("QCheckBox") || cls == "QLabel" || cls == "mv::ClickLabel" || c->inherits("QPlainTextEdit"))) continue;
                if (cls == "QPlainTextEdit" || c->inherits("QAbstractScrollArea")) {}
                const int right = c->mapTo(sa->viewport(), QPoint(c->width(), 0)).x();
                if (right > vw + 1) bad << QString("%1 right=%2 > viewport=%3").arg(cls).arg(right).arg(vw);
            }
            CHECK(bad.isEmpty() && !sa->horizontalScrollBar()->isVisible(), QString("%1: all controls fully visible at min width (viewport %2 px): %3").arg(what).arg(vw).arg(bad.join("; ")));
        };
        QString vidClip, titleClip; for (auto &c : m->project().clips) { if (c.isTitle()) titleClip = c.id; else if (m->project().mediaById(c.mediaId) && m->project().mediaById(c.mediaId)->kind == "video") vidClip = c.id; }
        m->selectClip(vidClip); check("video clip");
        w.findChild<QDockWidget *>("dockProps")->grab().save("/tmp/mveditor_props.png");
        if (!titleClip.isEmpty()) { m->selectClip(titleClip); check("title clip"); }
        m->selectClip({});
    }
    std::cout << "- tool-tip appearance" << std::endl;
    {
        QToolTip::showText(w.mapToGlobal(QPoint(300, 200)), "Split – cut the selected clip in two at the playhead (S)\nSecond line of the note", &w);
        QTest::qWait(300);
        QWidget *tip = nullptr;
        for (QWidget *t : QApplication::topLevelWidgets()) if (QString(t->metaObject()->className()) == "QTipLabel" && t->isVisible()) tip = t;
        CHECK(tip != nullptr, "tooltip window appears");
        if (tip) {
            CHECK(tip->windowOpacity() < 0.99 && tip->windowOpacity() > 0.6, QString("tooltip is slightly transparent (opacity %1)").arg(tip->windowOpacity()));
            const QImage img = tip->grab().toImage();
            const QColor bg = img.pixelColor(3, img.height() / 2);
            CHECK(bg.lightness() < 90, QString("tooltip background is dark grey (lightness %1)").arg(bg.lightness()));
            int bright = 0; for (int y = 0; y < img.height(); ++y) for (int x = 0; x < img.width(); ++x) if (img.pixelColor(x, y).lightness() > 200) ++bright;
            CHECK(bright > 50, QString("tooltip text is white (%1 bright pixels)").arg(bright));
            tip->grab().save("/tmp/mveditor_tip.png");
        }
        QToolTip::hideText();
    }
    {   // visual references: title panel and an open menu
        QString titleId; for (auto &c : m->project().clips) if (c.isTitle()) titleId = c.id;
        if (!titleId.isEmpty()) { m->selectClip(titleId); QTest::qWait(300); w.grab().save("/tmp/mveditor_title.png"); }
        QMenu *vm = nullptr; for (auto *mn : w.menuBar()->findChildren<QMenu *>()) if (mn->title().contains("Video")) vm = mn;
        if (vm) { vm->popup(w.mapToGlobal(QPoint(300, 60))); QTest::qWait(300); vm->grab().save("/tmp/mveditor_menu.png"); vm->close(); }
    }
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
