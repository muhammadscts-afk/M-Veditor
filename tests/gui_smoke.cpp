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
#include <QLineEdit>
#include <QKeyEvent>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
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
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
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
    std::cout << "- markers: per track, Ma/Mp pairs, colour only between a pair" << std::endl;
    {
        m->mutate("clear markers", [](Project &p) { p.markers.clear(); });
        auto addM = [&](double t, const QString &track) { m->mutate("marker", [&](Project &p) { Marker mk; mk.time = t; mk.trackId = track; p.markers.push_back(mk); }); };
        addM(3.0, trV1); addM(1.0, trV1); addM(2.0, trV1); addM(1.5, trA);
        const Project &pr = m->project();
        QStringList labels; for (int i : pr.markerIndexesOnTrack(trV1)) labels << pr.markerLabel(i);
        CHECK(labels == QStringList({"Ma", "Mp", "Ma"}), QString("markers on a track are named Ma, Mp, Ma ... in time order: %1").arg(labels.join(",")));
        CHECK(pr.markerIndexesOnTrack(trV1).size() == 3 && pr.markerIndexesOnTrack(trA).size() == 1 && pr.markerLabel(pr.markerIndexesOnTrack(trA).first()) == "Ma", "each track has its own marker sequence");
        addM(4.0, trV1);
        CHECK(pr.markerLabel(pr.markerIndexesOnTrack(trV1).last()) == "Mp", "4th marker completes the second pair (Mp)");
        const QColor c0 = TimelineView::pairColor(m->project(), trV1, 0), c1 = TimelineView::pairColor(m->project(), trV1, 1);
        CHECK(c0 != c1 && c0.alpha() >= 90 && c0.alpha() < 200, QString("pairs get different colours that are clearly visible but still see-through (alpha %1)").arg(c0.alpha()));
        // colour the first pair green, then orange: the new colour must be exactly the chosen one (never mixed with the previous)
        const int firstMa = m->project().markerIndexesOnTrack(trV1).first();
        m->mutate("c", [&](Project &p) { p.markers[firstMa].color = "#39ff14"; });
        QColor g1 = TimelineView::pairColor(m->project(), trV1, 0);
        m->mutate("c", [&](Project &p) { p.markers[firstMa].color = "#ff9100"; });
        QColor g2 = TimelineView::pairColor(m->project(), trV1, 0);
        CHECK(g1.red() == 0x39 && g1.green() == 0xff && g2.red() == 0xff && g2.green() == 0x91 && g2.blue() == 0 && TimelineView::pairColor(m->project(), trV1, 1) == c1,
              "recolouring uses exactly the new colour, other pairs are untouched");
        m->setActiveTrack(trA); w.findChild<QAction *>("marker")->trigger();
        CHECK(m->project().markerIndexesOnTrack(trA).size() == 2, "the M key adds a marker to the active (clicked) track");
        m->mutate("c", [&](Project &p) { p.markers[firstMa].color = "#9fe8ff"; });
        QTest::qWait(200);
        w.grab().save("/tmp/mveditor_regions.png");
    }
    std::cout << "- solo is exclusive (mouse clicks on the S buttons)" << std::endl;
    {
        w.timeline()->verticalScrollBar()->setValue(0);
        auto topOf = [&](int i) { int t = 30; for (int k = 0; k < i; ++k) t += m->project().tracks[k].height; return t; };
        auto clickS = [&](int i, Qt::KeyboardModifiers mod = Qt::NoModifier) { QTest::mouseClick(w.timeline()->viewport(), Qt::LeftButton, mod, QPoint(125, topOf(i) + 15)); QTest::qWait(30); };
        auto solo = [&](int i) { return m->project().tracks[i].solo; };
        clickS(1); CHECK(solo(1) && !solo(0) && !solo(2), "click S on track 2: it is soloed");
        clickS(2); CHECK(solo(2) && !solo(1), "click S on another track switches the previous solo OFF");
        clickS(1, Qt::ControlModifier); CHECK(solo(1) && solo(2), "Ctrl+click S solos several tracks");
        clickS(2); CHECK(!solo(2) && solo(1), "clicking a soloed S turns it off");
        clickS(1); CHECK(!solo(1), "all solos off again");
        w.timeline()->viewport()->update();
    }
    std::cout << "- speed change anchors on whichever selected clip is under the playhead" << std::endl;
    {
        Project saved = m->project(); const QString keepPath = m->filePath();
        MediaItem v0; for (auto &mi : saved.media) if (mi.kind == "video") v0 = mi;
        Project two = Project::singleClip(v0, 0, 4.0); two.tracks.clear(); two.ensureDefaultTracks();
        QString v1id; for (auto &t : two.tracks) if (t.name == "Video 1") v1id = t.id;
        two.clips[0].trackId = v1id;
        Clip c2 = two.clips[0]; c2.id = "c2x"; c2.start = 4.0; c2.in = 4.0; c2.out = 8.0; two.clips.push_back(c2);
        m->reset(two, QString());
        m->setSelection({"c2x", "c1"});                      // main clip is c1, but the playhead is inside c2
        m->setPlayhead(5.5);
        if (QWidget *f = QApplication::focusWidget()) f->clearFocus();
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_BracketRight, Qt::NoModifier, "]"); QApplication::sendEvent(&w, &ev);
        const Clip *cc = m->project().clipById("c2x");
        const double moment = cc->in + (m->playhead() - cc->start) * cc->fx.speed;
        CHECK(near(cc->fx.speed, 1.1, 1e-9) && near(m->project().clipById("c1")->fx.speed, 1.1, 1e-9), "both selected clips change speed");
        CHECK(near(moment, 5.5, 1e-6), QString("the playhead stays on the same frame of the clip it was in (%1)").arg(moment));
        m->reset(saved, keepPath);
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

    std::cout << "- keyboard: arrows, Home/End, Arabic keys, speed steps keep the same frame" << std::endl;
    {
        auto *prev = w.findChild<PreviewMonitor *>();
        prev->pause();
        Project saved = m->project(); const QString keepPath = m->filePath();
        MediaItem v0; for (auto &mi : saved.media) if (mi.kind == "video") v0 = mi;
        Project single = Project::singleClip(v0, 0, 8.0); single.tracks.clear(); single.ensureDefaultTracks();
        for (auto &t : single.tracks) if (t.name == "Video 1") single.clips[0].trackId = t.id;
        m->reset(single, QString());
        const QString cid = m->project().clips[0].id;
        m->selectClip(cid);
        if (QWidget *f = QApplication::focusWidget()) f->clearFocus();
        auto press = [&](int key, const QString &text) { QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text); QApplication::sendEvent(&w, &ev); };
        const QString ZAY = QString::fromUtf8("\xd8\xb2"), WAW = QString::fromUtf8("\xd9\x88"), DAL = QString::fromUtf8("\xd8\xaf"), JEEM = QString::fromUtf8("\xd8\xac");
        const double fps = m->project().settings.fps;
        m->setPlayhead(1.0);
        press(Qt::Key_Right, ""); CHECK(near(m->playhead(), 6.0, 1e-6), QString("Right arrow jumps 5 s forward (%1)").arg(m->playhead()));
        press(Qt::Key_Left, "");  CHECK(near(m->playhead(), 1.0, 1e-6), QString("Left arrow jumps 5 s back (%1)").arg(m->playhead()));
        press(Qt::Key_Up, "");    CHECK(near(m->playhead(), 1.0 + 1.0 / fps, 1e-6), QString("Up arrow = next frame (%1)").arg(m->playhead()));
        press(Qt::Key_Down, ""); press(Qt::Key_Down, ""); CHECK(near(m->playhead(), 1.0 - 1.0 / fps, 1e-6), QString("Down arrow = previous frame (%1)").arg(m->playhead()));
        m->setPlayhead(3.0);
        press(Qt::Key_Period, "."); CHECK(near(m->playhead(), 4.0, 1e-6), "'.' key jumps 1 s forward");
        press(0x632, ZAY);          CHECK(near(m->playhead(), 5.0, 1e-6), "Arabic ز jumps 1 s forward");
        press(0x648, WAW);          CHECK(near(m->playhead(), 4.0, 1e-6), "Arabic و jumps 1 s back");
        press(Qt::Key_Comma, ",");  CHECK(near(m->playhead(), 3.0, 1e-6), "',' key jumps 1 s back");
        w.findChild<QAction *>("end")->trigger();  CHECK(near(m->playhead(), 8.0, 1e-6), "End goes to the end");
        w.findChild<QAction *>("home")->trigger(); CHECK(near(m->playhead(), 0.0, 1e-6), "Home goes to the start");
        press(Qt::Key_Left, ""); CHECK(near(m->playhead(), 0.0, 1e-6), "jumps never go before the start");
        // ---- speed steps: the picture must stay on the same source frame
        m->setPlayhead(4.0);
        auto moment = [&]() { const Clip *c = m->project().clipById(cid); return c->in + (m->playhead() - c->start) * c->fx.speed; };
        auto speedNow = [&]() { return m->project().clipById(cid)->fx.speed; };
        press(Qt::Key_BracketRight, "]"); CHECK(near(speedNow(), 1.1, 1e-9) && near(moment(), 4.0, 1e-6), QString("] speeds up by 0.1 (%1) and stays on the same frame (%2)").arg(speedNow()).arg(moment()));
        press(0x62F, DAL);                CHECK(near(speedNow(), 1.2, 1e-9) && near(moment(), 4.0, 1e-6), QString("Arabic د speeds up by 0.1 (%1), frame kept (%2)").arg(speedNow()).arg(moment()));
        press(0x62C, JEEM);               CHECK(near(speedNow(), 1.1, 1e-9) && near(moment(), 4.0, 1e-6), QString("Arabic ج slows down by 0.1 (%1), frame kept (%2)").arg(speedNow()).arg(moment()));
        press(Qt::Key_BracketLeft, "[");  CHECK(near(speedNow(), 1.0, 1e-9) && near(moment(), 4.0, 1e-6) && near(m->playhead(), 4.0, 1e-6), "[ slows down by 0.1 and the playhead returns to the same spot");
        press(Qt::Key_Equal, "=");  CHECK(near(speedNow(), 1.1, 1e-9) && near(moment(), 4.0, 1e-6), "the = key speeds up by 0.1, frame kept");
        press(Qt::Key_Minus, "-");  CHECK(near(speedNow(), 1.0, 1e-9) && near(moment(), 4.0, 1e-6), "the - key slows down by 0.1, frame kept");
        for (int i = 0; i < 20; ++i) press(Qt::Key_BracketRight, "]");
        CHECK(near(speedNow(), 3.0, 1e-9) && near(moment(), 4.0, 1e-6), QString("20 presses = 3.0x, frame kept (%1)").arg(moment()));
        m->undoStack()->undo();
        CHECK(near(speedNow(), 1.0, 1e-9), QString("all the quick key presses are ONE undo step (back to %1)").arg(speedNow()));
        m->mutate("slow", [&](Project &p) { p.clipById(cid)->fx.speed = 0.1; });
        press(Qt::Key_BracketLeft, "["); CHECK(near(speedNow(), 0.1, 1e-9), "speed cannot go below 0.1x");
        // typing in a text field must not be hijacked
        auto *le = w.findChildren<QLineEdit *>().isEmpty() ? nullptr : w.findChildren<QLineEdit *>().first();
        if (le) { le->setFocus(); QTest::qWait(60); if (QApplication::focusWidget() == le) { const double before = speedNow(); press(0x62F, DAL); CHECK(near(speedNow(), before, 1e-9), "typing Arabic letters in a text box does not change the speed"); } }
        m->reset(saved, keepPath);
    }
    std::cout << "- redo shortcut, gaps (Clipchamp style), tint over clips" << std::endl;
    {
        // Ctrl+Y must be registered exactly once (a duplicate made Qt treat it as ambiguous and ignore it)
        int owners = 0; QAction *redoAct = nullptr; bool distinct = true;
        for (QAction *ac : w.findChildren<QAction *>()) if (ac->shortcuts().contains(QKeySequence("Ctrl+Y"))) { ++owners; redoAct = ac; QList<QKeySequence> l = ac->shortcuts(); for (int i = 0; i < l.size(); ++i) for (int j = i + 1; j < l.size(); ++j) if (l[i] == l[j]) distinct = false; }
        CHECK(owners == 1 && distinct && redoAct && redoAct->shortcuts().size() >= 2, QString("Ctrl+Y belongs to exactly one action with distinct shortcuts (%1 owners)").arg(owners));
        Project saved = m->project(); const QString keepPath = m->filePath();
        MediaItem v0; for (auto &mi : saved.media) if (mi.kind == "video") v0 = mi;
        Project proj = Project::singleClip(v0, 0, 2); proj.tracks.clear(); proj.ensureDefaultTracks();
        QString v1id; int v1idx = 0; for (int i = 0; i < proj.tracks.size(); ++i) if (proj.tracks[i].name == "Video 1") { v1id = proj.tracks[i].id; v1idx = i; }
        proj.clips.clear();
        const double starts[] = {0, 5, 10}; int n = 0;
        for (double st : starts) { Clip c; c.id = QString("g%1").arg(n++); c.mediaId = v0.id; c.trackId = v1id; c.start = st; c.in = 0; c.out = 2; proj.clips.push_back(c); }
        m->reset(proj, QString());
        w.timeline()->verticalScrollBar()->setValue(0); w.timeline()->zoomFit(); QTest::qWait(250);
        QWidget *vp = w.timeline()->viewport();
        auto X = [&](double t) { return int(170 + t * w.timeline()->zoom() - w.timeline()->horizontalScrollBar()->value()); };
        int top = 30; for (int i = 0; i < v1idx; ++i) top += m->project().tracks[i].height;
        const int cy = top + m->project().tracks[v1idx].height / 2;
        // ---- gap: hover + click the bin button
        CHECK(m->project().gapsOnTrack(v1id).size() == 2, "two gaps (2-5 s and 7-10 s)");
        QTest::mouseMove(vp, QPoint(X(3.5), cy)); QTest::qWait(100);
        QImage hov = vp->grab().toImage();
        QTest::mouseMove(vp, QPoint(X(0.9), cy)); QTest::qWait(100);             // not hovering a gap: the bin disappears
        QImage nohov = vp->grab().toImage();
        hov.save("/tmp/mveditor_gap.png");
        int differ = 0; for (int y = cy - 14; y <= cy + 14; ++y) for (int x = X(3.5) - 14; x <= X(3.5) + 14; ++x) if (hov.pixelColor(x, y) != nohov.pixelColor(x, y)) ++differ;
        CHECK(differ > 150, QString("hovering a gap shows a delete (bin) button (%1 changed pixels)").arg(differ));
        QTest::mouseMove(vp, QPoint(X(3.5), cy)); QTest::qWait(60);
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, QPoint(X(3.5), cy)); QTest::qWait(100);
        CHECK(near(m->project().clipById("g1")->start, 2.0, 1e-6) && near(m->project().clipById("g2")->start, 7.0, 1e-6) && near(m->project().clipById("g0")->start, 0.0, 1e-6), QString("clicking the bin deleted the gap: later clips moved back (g1 at %1, g2 at %2)").arg(m->project().clipById("g1")->start).arg(m->project().clipById("g2")->start));
        m->undoStack()->undo();
        CHECK(near(m->project().clipById("g1")->start, 5.0, 1e-6), "deleting a gap can be undone");
        w.timeline()->deleteAllGaps(v1id);
        CHECK(near(m->project().clipById("g1")->start, 2.0, 1e-6) && near(m->project().clipById("g2")->start, 4.0, 1e-6), "Delete all gaps packs the track");
        m->undoStack()->undo();
        // ---- marker tint is drawn OVER the clips
        m->mutate("pair", [&](Project &p) { Marker a; a.time = 0.2; a.trackId = v1id; a.color = "#39ff14"; Marker b; b.time = 1.9; b.trackId = v1id; p.markers.push_back(a); p.markers.push_back(b); });
        QTest::mouseMove(vp, QPoint(X(3.5), 4)); QTest::qWait(150);           // pointer somewhere harmless
        w.timeline()->setRegionsVisible(true); QTest::qWait(100);
        const QColor on = vp->grab().toImage().pixelColor(X(1.7), cy);
        w.timeline()->setRegionsVisible(false); QTest::qWait(100);
        const QColor off = vp->grab().toImage().pixelColor(X(1.7), cy);
        w.timeline()->setRegionsVisible(true); QTest::qWait(100);
        w.grab().save("/tmp/mveditor_tint.png");
        CHECK(on.green() > off.green() + 3, QString("the colour between Ma and Mp is visible on top of the clip (green %1 vs %2 without)").arg(on.green()).arg(off.green()));
        m->undoStack()->undo();
        // ---- Ctrl+Y really redoes (when the window is active)
        m->mutate("tmp", [&](Project &p) { p.clipById("g1")->start = 6.0; });
        m->undoStack()->undo();
        if (QApplication::activeWindow()) { QTest::keyClick(&w, Qt::Key_Y, Qt::ControlModifier); QTest::qWait(60); CHECK(near(m->project().clipById("g1")->start, 6.0, 1e-6), "pressing Ctrl+Y redoes the change"); }
        m->reset(saved, keepPath);
    }
    std::cout << "- source speed (Speed icon active while playing the source) and full screen" << std::endl;
    {
        auto *prev = w.findChild<PreviewMonitor *>();
        QString vidId; for (auto &mi : m->project().media) if (mi.kind == "video") vidId = mi.id;
        m->selectClip({});
        prev->playSource(vidId); QTest::qWait(250);
        CHECK(prev->mode() == PreviewMonitor::Mode::Source, "Source mode");
        QAction *spd = w.findChild<QAction *>("speed");
        CHECK(spd && spd->isEnabled() && w.findChild<QAction *>("speedup")->isEnabled(), "the Speed icon is active while the source is open, even with no clip selected");
        if (QWidget *f = QApplication::focusWidget()) f->clearFocus();
        auto press = [&](int key, const QString &text) { QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text); QApplication::sendEvent(&w, &ev); };
        press(Qt::Key_BracketRight, "]"); press(0x62F, QString::fromUtf8("\xd8\xaf"));
        CHECK(near(prev->sourceSpeed(), 1.2, 1e-9), QString("] and د change the source preview speed (%1)").arg(prev->sourceSpeed()));
        press(Qt::Key_BracketLeft, "["); CHECK(near(prev->sourceSpeed(), 1.1, 1e-9), "[ slows it down");
        prev->setSourceSpeed(2.0); CHECK(near(prev->sourceSpeed(), 2.0, 1e-9), "speed can be set directly (dialog)");
        prev->setSourceSpeed(40.0); CHECK(near(prev->sourceSpeed(), 16.0, 1e-9), "source speed is limited to 16x");
        prev->playSource(vidId); CHECK(near(prev->sourceSpeed(), 1.0, 1e-9), "opening a source starts at normal speed");
        // ---- full screen: double-click the picture
        QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(50, 50), QPointF(50, 50), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(prev->view()->viewport(), &dbl); QTest::qWait(500);
        FullscreenWindow *fs = prev->fullscreenWindow();
        CHECK(fs && fs->isFullScreen() && prev->isFullscreen(), "double-clicking the picture goes full screen");
        QGraphicsView *gv = prev->view();
        const QRectF pic = gv->mapFromScene(gv->sceneRect()).boundingRect();
        CHECK(fs && gv->parentWidget() == fs && (pic.width() >= gv->viewport()->width() * 0.95 || pic.height() >= gv->viewport()->height() * 0.95),
              QString("the picture FILLS the full-screen window (picture %1x%2 in a %3x%4 view)").arg(int(pic.width())).arg(int(pic.height())).arg(gv->viewport()->width()).arg(gv->viewport()->height()));
        if (fs) fs->grab().save("/tmp/mveditor_fullscreen.png");
        CHECK(fs && fs->barVisible(), "the seek bar is shown when entering full screen");
        QTest::qWait(3000);
        CHECK(fs && !fs->barVisible(), "the seek bar hides itself after the mouse is idle");
        if (fs) { QMouseEvent me(QEvent::MouseMove, QPointF(300, 300), QPointF(300, 300), Qt::NoButton, Qt::NoButton, Qt::NoModifier); QApplication::sendEvent(fs->viewport(), &me); QTest::qWait(150); }
        CHECK(fs && fs->barVisible(), "moving the mouse brings the seek bar back");
        QApplication::sendEvent(prev->view()->viewport(), &dbl); QTest::qWait(300);
        CHECK(!prev->isFullscreen() && prev->view()->parentWidget() == prev && prev->view()->isVisible(), "double-clicking again leaves full screen and the picture is back in the monitor");
        prev->toggleFullscreen(); QTest::qWait(400);                                  // F11 / menu path
        fs = prev->fullscreenWindow();
        CHECK(fs && fs->isFullScreen(), "F11 / menu also enters full screen");
        if (fs) { QTest::keyClick(fs, Qt::Key_Escape); QTest::qWait(250); }
        CHECK(!prev->isFullscreen() && prev->view()->parentWidget() == prev, "Esc leaves full screen");
        QToolButton *tlb = nullptr; for (auto *b : prev->findChildren<QToolButton *>()) if (b->text() == "Timeline") tlb = b;
        if (tlb) tlb->click();
    }
    std::cout << "- volume control and A-B repeat" << std::endl;
    {
        auto *prev = w.findChild<PreviewMonitor *>();
        QToolButton *liveB = nullptr; for (auto *b : prev->findChildren<QToolButton *>()) if (b->text() == "Live") liveB = b;
        if (liveB && !liveB->isChecked()) liveB->setChecked(true);
        auto *vb = prev->findChild<VolumeBox *>();
        CHECK(vb != nullptr, "a volume control sits next to the time");
        QString vidId; for (auto &mi : m->project().media) if (mi.kind == "video") vidId = mi.id;
        prev->playSource(vidId); QTest::qWait(200);
        prev->setMasterVolume(40);
        CHECK(prev->volumePercent() == 40 && near(prev->outputVolume(), 0.4, 0.02), QString("volume 40%% reaches the audio output (%1)").arg(prev->outputVolume()));
        prev->setMuted(true);  CHECK(prev->outputVolume() < 0.001, "mute silences the output");
        prev->setMuted(false); prev->setMasterVolume(100); CHECK(near(prev->outputVolume(), 1.0, 0.02), "full volume again");
        QToolButton *spk = vb ? vb->findChild<QToolButton *>() : nullptr;
        if (spk) { spk->click(); CHECK(prev->isMuted(), "the speaker button mutes"); spk->click(); CHECK(!prev->isMuted(), "and unmutes"); }
        QSlider *vs = vb ? vb->findChild<QSlider *>() : nullptr;
        if (vs) { vs->setValue(25); CHECK(prev->volumePercent() == 25, "the volume slider changes the volume"); vs->setValue(100); }
        QToolButton *tlb2 = nullptr; for (auto *b : prev->findChildren<QToolButton *>()) if (b->text() == "Timeline") tlb2 = b;
        if (tlb2) tlb2->click();
        // ---- A-B repeat on the timeline
        prev->pause(); m->setPlayhead(3.0);
        prev->abClick(); CHECK(prev->abState() == 1, "1st click on A B sets point A");
        prev->abClick(); CHECK(prev->abState() == 1, "point B at the same place as A is refused");
        m->setPlayhead(4.0); prev->abClick(); CHECK(prev->abState() == 2, "2nd click sets point B: repeating");
        prev->abClick(); CHECK(prev->abState() == 0, "3rd click stops repeating");
        m->setPlayhead(1.0); prev->abClick(); m->setPlayhead(2.0); prev->abClick();
        CHECK(prev->abState() == 2, "A=1.0 s, B=2.0 s");
        m->setPlayhead(1.7); prev->togglePlay(); QTest::qWait(1100);
        const double ph = m->playhead(); prev->pause();
        CHECK(ph >= 0.95 && ph <= 2.1, QString("playback repeats between A and B (the playhead is at %1 s, not past 2.0)").arg(ph));
        prev->abClick(); CHECK(prev->abState() == 0, "clicking the A B button stops the repeat");
        m->setPlayhead(0);
    }
    std::cout << "- Extract all button" << std::endl;
    {
        auto *prev = w.findChild<PreviewMonitor *>();
        QToolButton *ex = nullptr; for (auto *b : prev->findChildren<QToolButton *>()) if (b->text() == "Extract all") ex = b;
        CHECK(ex != nullptr, "an 'Extract all' button sits beside the volume control");
        Project saved = m->project(); const QString keepPath = m->filePath();
        MediaItem v0; for (auto &mi : saved.media) if (mi.kind == "video") v0 = mi;
        Project proj = Project::singleClip(v0, 0, 6); proj.tracks.clear(); proj.ensureDefaultTracks();
        QString v1id, v2id; for (auto &t : proj.tracks) { if (t.name == "Video 1") v1id = t.id; if (t.name == "Video 2") v2id = t.id; }
        proj.clips.clear();
        Clip a; a.id = "ca"; a.mediaId = v0.id; a.trackId = v1id; a.start = 0; a.in = 0; a.out = 6; proj.clips.push_back(a);
        Marker m1; m1.time = 1; m1.trackId = v1id; Marker m2; m2.time = 3; m2.trackId = v1id; proj.markers = {m1, m2};
        m->reset(proj, QString());
        m->setActiveTrack(v1id);
        if (QWidget *f = QApplication::focusWidget()) f->clearFocus();
        w.timeline()->zoomFit(); QTest::qWait(150); w.grab().save("/tmp/mveditor_extract_before.png");
        if (ex) ex->click(); QTest::qWait(150);
        w.grab().save("/tmp/mveditor_extract_after.png");
        QVector<const Clip *> low = m->project().clipsOnTrack(v2id), up = m->project().clipsOnTrack(v1id);
        CHECK(low.size() == 1 && near(low[0]->start, 1.0, 1e-6) && near(low[0]->duration(), 2.0, 1e-6), "clicking Extract all moved the marked part to the track below at the same position");
        CHECK(up.size() == 2 && near(up[0]->end(), 1.0, 1e-6) && near(up[1]->start, 3.0, 1e-6), "the original track keeps the rest, with a gap where the part was");
        CHECK(m->selectedClips().size() == 1, "the extracted segment is selected");
        m->undoStack()->undo();
        CHECK(m->project().clips.size() == 1 && m->project().trackById(v2id) && m->project().clipsOnTrack(v2id).isEmpty(), "everything happens in ONE undo step");
        if (ex) { ex->click(); ex->click(); QTest::qWait(60); }                                  // second click: nothing new to extract (the pieces already moved)
        CHECK(m->project().clipsOnTrack(v1id).size() == 2, "clicking again does not duplicate or damage anything");
        m->reset(saved, keepPath);
    }
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
            CHECK(tip->windowOpacity() < 0.8 && tip->windowOpacity() > 0.4, QString("tooltip is clearly transparent (opacity %1)").arg(tip->windowOpacity()));
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
