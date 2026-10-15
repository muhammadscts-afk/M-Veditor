#include <functional>
#include "MainWindow.h"
#include "AppSettings.h"
#include "Dialogs.h"
#include "FfmpegTools.h"
#include "MediaProbe.h"
#include "Theme.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDirIterator>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSaveFile>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace mv {

static const char *kMediaFilter =
    "Media files (*.mp4 *.mov *.avi *.mkv *.webm *.m4v *.wmv *.flv *.mpg *.mpeg *.ts *.mts *.3gp *.mp3 *.wav *.aac *.m4a *.flac *.ogg *.opus *.wma *.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;All files (*)";

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setAcceptDrops(true);
    setWindowIcon(QIcon(":/icon.png"));
    qRegisterMetaType<mv::ProbeResult>();
    qRegisterMetaType<QVector<float>>();
    m = new ProjectModel(this);
    m_thumbs = new ThumbnailGenerator(this);
    connect(m_thumbs, &ThumbnailGenerator::ready, this, [this](const QString &id, const QString &p) { m->setThumbnail(id, p); });

    m_tl = new TimelineView(m);
    m_tl->setSnapping(AppSettings::snapping());
    m_prev = new PreviewMonitor(m);
    auto *split = new QSplitter(Qt::Vertical);
    split->setObjectName("centerSplit");
    split->addWidget(m_prev); split->addWidget(m_tl);
    split->setStretchFactor(0, 1); split->setStretchFactor(1, 1);
    split->setSizes({380, 400});
    split->setChildrenCollapsible(false);
    setCentralWidget(split);

    buildDocks();
    buildActions();
    buildMenus();
    buildToolbar();

    m_bar = new QProgressBar; m_bar->setRange(0, 1000); m_bar->setFixedWidth(220); m_bar->setVisible(false); m_bar->setTextVisible(true);
    m_etaLabel = new QLabel; m_stateLabel = new QLabel(tr("Ready"));
    statusBar()->addPermanentWidget(m_etaLabel); statusBar()->addPermanentWidget(m_bar); statusBar()->addPermanentWidget(m_stateLabel);
    statusBar()->showMessage(tr("Welcome to M Veditor. Import media to begin."), 6000);

    connect(m_tl, &TimelineView::mediaDropped, this, [this](const QString &id, const QString &tr, double s) { addClipFromMedia(id, tr, s); });
    connect(m_tl, &TimelineView::clipContextMenu, this, &MainWindow::showClipMenu);
    connect(m_tl, &TimelineView::trackContextMenu, this, &MainWindow::showTrackMenu);
    connect(m_tl, &TimelineView::message, this, [this](const QString &t) { notify(t); });
    connect(m_tl, &TimelineView::rulerContextMenu, this, [this](double t, const QPoint &gp) {
        QMenu menu(this);
        menu.addAction(tr("Add marker here"), [this, t] { addMarker(t); });
        menu.addAction(tr("Delete nearest marker"), [this, t] {
            int best = -1; double bd = 1e9; const auto &mk = m->project().markers;
            for (int i = 0; i < mk.size(); ++i) if (std::fabs(mk[i].time - t) < bd) { bd = std::fabs(mk[i].time - t); best = i; }
            if (best >= 0) m->mutate(tr("Delete marker"), [best](Project &p) { p.markers.removeAt(best); });
        });
        menu.addAction(tr("Rename nearest marker…"), [this, t] {
            int best = -1; double bd = 1e9; const auto &mk = m->project().markers;
            for (int i = 0; i < mk.size(); ++i) if (std::fabs(mk[i].time - t) < bd) { bd = std::fabs(mk[i].time - t); best = i; }
            if (best < 0) return;
            bool ok; QString n = QInputDialog::getText(this, tr("Marker"), tr("Name:"), QLineEdit::Normal, mk[best].name, &ok);
            if (ok) m->mutate(tr("Rename marker"), [best, n](Project &p) { p.markers[best].name = n; });
        });
        menu.exec(gp);
    });
    connect(m_lib, &MediaLibrary::importRequested, a["import"], &QAction::trigger);
    connect(m_lib, &MediaLibrary::filesDropped, this, &MainWindow::importFiles);
    connect(m_lib, &MediaLibrary::removeRequested, this, &MainWindow::removeMedia);
    connect(m_lib, &MediaLibrary::renameRequested, this, &MainWindow::renameMedia);
    connect(m_lib, &MediaLibrary::previewRequested, m_prev, &PreviewMonitor::playSource);
    connect(m_lib, &MediaLibrary::contextMenuRequested, this, [this](const QString &id, const QPoint &gp) {
        QMenu menu(this);
        menu.addAction(tr("Preview"), [this, id] { m_prev->playSource(id); });
        menu.addAction(tr("Add to timeline at playhead"), [this, id] { addClipFromMedia(id, {}, m->playhead()); });
        menu.addAction(tr("Media information…"), [this, id] {
            const MediaItem *mi = m->project().mediaById(id); if (!mi) return;
            ProbeResult r = probeBlocking(mi->path);
            MediaInfoDialog(r.ok ? r.item : *mi, r.rawJson, this).exec();
        });
        menu.addAction(tr("Create optimized preview (proxy)"), [this, id] { createProxy(id); });
        menu.addSeparator();
        menu.addAction(tr("Rename…"), [this, id] { renameMedia(id); });
        menu.addAction(tr("Remove from project"), [this, id] { removeMedia(id); });
        menu.exec(gp);
    });
    connect(m_prev, &PreviewMonitor::renderPreviewRequested, this, &MainWindow::renderPreview);
    connect(m_prev, &PreviewMonitor::message, this, [this](const QString &t) { notify(t); });
    connect(m_prev, &PreviewMonitor::addToTimelineRequested, this, [this](const QString &id, double in, double out) { addClipFromMedia(id, {}, m->playhead(), in, out); });
    connect(m_props, &PropertiesPanel::speedDialogRequested, this, &MainWindow::applySpeedDialog);
    connect(m_exp, &ExportPanel::exportRequested, this, &MainWindow::startExport);
    connect(m_exp, &ExportPanel::cancelRequested, this, [this] { if (m_job) m_job->cancel(); });
    connect(m, &ProjectModel::selectionChanged, this, &MainWindow::updateActions);
    connect(m, &ProjectModel::changed, this, &MainWindow::updateActions);
    connect(m, &ProjectModel::titleChanged, this, &MainWindow::updateTitle);

    connect(&m_autosave, &QTimer::timeout, this, &MainWindow::autosave);
    const int mins = AppSettings::autosaveMinutes();
    if (mins > 0) m_autosave.start(mins * 60 * 1000);

    m_lock = std::make_unique<QLockFile>(AppSettings::dataDir() + "/session.lock");
    m_lock->setStaleLockTime(0);
    m_primaryInstance = m_lock->tryLock(100);

    restoreGeometry(AppSettings::s().value("ui/geometry").toByteArray());
    restoreState(AppSettings::s().value("ui/state").toByteArray());
    split->restoreState(AppSettings::s().value("ui/split").toByteArray());
    updateActions(); updateTitle();
}

MainWindow::~MainWindow() = default;

void MainWindow::notify(const QString &text, bool error) {
    m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss ") + (error ? "ERROR: " : "") + text);
    statusBar()->showMessage(text, error ? 12000 : 6000);
    if (error) m_stateLabel->setText(tr("<span style='color:#ff6b6b'>Error – see Log</span>"));
}

void MainWindow::updateTitle() {
    const QString name = m->filePath().isEmpty() ? m->project().name : QFileInfo(m->filePath()).completeBaseName();
    setWindowTitle(QString("%1%2 – M Veditor").arg(name, m->isDirty() ? "*" : ""));
}

// ======================================================================== docks
void MainWindow::buildDocks() {
    m_lib = new MediaLibrary(m);
    m_dockLib = new QDockWidget(tr("Media library")); m_dockLib->setObjectName("dockLib"); m_dockLib->setWidget(m_lib);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockLib);
    m_props = new PropertiesPanel(m);
    m_dockProps = new QDockWidget(tr("Properties")); m_dockProps->setObjectName("dockProps"); m_dockProps->setWidget(m_props);
    addDockWidget(Qt::RightDockWidgetArea, m_dockProps);
    m_exp = new ExportPanel(m);
    m_dockExp = new QDockWidget(tr("Export")); m_dockExp->setObjectName("dockExport"); m_dockExp->setWidget(m_exp);
    addDockWidget(Qt::RightDockWidgetArea, m_dockExp);
    m_log = new QPlainTextEdit; m_log->setReadOnly(true); m_log->setMaximumBlockCount(2000);
    m_dockLog = new QDockWidget(tr("Activity log")); m_dockLog->setObjectName("dockLog"); m_dockLog->setWidget(m_log);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockLog);
    m_dockLog->hide();
    tabifyDockWidget(m_dockProps, m_dockExp);
    m_dockProps->raise();
    m_dockLib->setMinimumWidth(240); m_dockProps->setMinimumWidth(300);
    resizeDocks({m_dockLib, m_dockProps}, {260, 330}, Qt::Horizontal);
}

// ======================================================================== actions
void MainWindow::buildActions() {
    auto add = [&](const QString &id, const QString &text, const QKeySequence &ks, auto slot, const QString &tip = {}) {
        auto *ac = new QAction(text, this);
        if (!ks.isEmpty()) ac->setShortcut(ks);
        if (!tip.isEmpty()) { ac->setToolTip(tip); ac->setStatusTip(tip); }
        connect(ac, &QAction::triggered, this, slot);
        addAction(ac);
        a[id] = ac; ac->setObjectName(id);
        return ac;
    };
    add("new", tr("&New project"), QKeySequence::New, [this] { newProject(); });
    add("open", tr("&Open project…"), QKeySequence::Open, [this] { openDialog(); });
    add("save", tr("&Save project"), QKeySequence::Save, [this] { saveProject(); });
    add("saveas", tr("Save project &as…"), QKeySequence::SaveAs, [this] { saveProjectAs(); });
    add("import", tr("&Import media…"), QKeySequence("Ctrl+I"), [this] {
        const QStringList f = QFileDialog::getOpenFileNames(this, tr("Import media"), AppSettings::importDir(), tr(kMediaFilter));
        if (!f.isEmpty()) { AppSettings::setImportDir(QFileInfo(f.first()).absolutePath()); importFiles(f); }
    }, tr("Import video, audio and image files"));
    add("relink", tr("Relink missing media…"), {}, [this] { relinkMissing(); });
    add("exit", tr("E&xit"), QKeySequence("Alt+F4"), [this] { close(); });

    add("copy", tr("&Copy clip"), QKeySequence::Copy, [this] { copySelected(false); });
    add("cut", tr("Cu&t clip"), QKeySequence::Cut, [this] { copySelected(true); });
    add("paste", tr("&Paste clip"), QKeySequence::Paste, [this] { pasteClip(); });
    add("dup", tr("&Duplicate clip"), QKeySequence("Ctrl+D"), [this] { duplicateSelected(); });
    add("split", tr("&Split at playhead"), QKeySequence("S"), [this] { splitAtPlayhead(); }, tr("Cut the selected clip (or every clip under the playhead) in two (S)"));
    add("delete", tr("&Delete clip"), QKeySequence::Delete, [this] { deleteSelected(false); }, tr("Delete the selected clip (Del)"));
    add("ripple", tr("Ripple delete (close the gap)"), QKeySequence("Shift+Delete"), [this] { deleteSelected(true); });
    add("marker", tr("Add &marker"), QKeySequence("M"), [this] { addMarker(m->playhead()); });
    add("snap", tr("Snapping"), {}, [this] { m_tl->setSnapping(a["snap"]->isChecked()); AppSettings::setSnapping(a["snap"]->isChecked()); });
    a["snap"]->setCheckable(true); a["snap"]->setChecked(AppSettings::snapping());

    add("speed", tr("&Speed controller…"), QKeySequence("Ctrl+R"), [this] { applySpeedDialog(); }, tr("Slow motion / fast motion"));
    add("reverse", tr("&Reverse"), {}, [this] { editSelected(tr("Reverse"), [](Clip &c) { c.fx.reverse = !c.fx.reverse; }); });
    add("rot", tr("Rotate 90° clockwise"), QKeySequence("Ctrl+Shift+R"), [this] { editSelected(tr("Rotate"), [](Clip &c) { c.fx.rotation = (c.fx.rotation + 90) % 360; }); });
    add("fliph", tr("Flip horizontally"), {}, [this] { editSelected(tr("Flip"), [](Clip &c) { c.fx.flipH = !c.fx.flipH; }); });
    add("flipv", tr("Flip vertically"), {}, [this] { editSelected(tr("Flip"), [](Clip &c) { c.fx.flipV = !c.fx.flipV; }); });
    add("title", tr("Add &title"), QKeySequence("T"), [this] { addTitle("title"); });
    add("subtitle", tr("Add &subtitle"), {}, [this] { addTitle("subtitle"); });
    add("caption", tr("Add &caption"), {}, [this] { addTitle("caption"); });
    add("watermark", tr("Add &watermark / logo…"), {}, [this] { addWatermark(); });
    add("pip", tr("Make picture-in-picture"), {}, [this] { applyPip(); });
    add("detach", tr("&Detach audio from video"), QKeySequence("Ctrl+Shift+D"), [this] { detachAudio(); });
    add("track_v", tr("Add video track"), {}, [this] { addTrack(TrackType::Video); });
    add("track_a", tr("Add audio track"), {}, [this] { addTrack(TrackType::Audio); });
    add("track_m", tr("Add music track"), {}, [this] { addTrack(TrackType::Music); });
    add("track_vo", tr("Add voice-over track"), {}, [this] { addTrack(TrackType::Voice); });
    add("track_t", tr("Add title track"), {}, [this] { addTrack(TrackType::Title); });

    add("extract", tr("&Extract audio…"), {}, [this] { extractAudio(); }, tr("Save the sound of a video as MP3, WAV, AAC or FLAC"));
    add("mute", tr("&Mute clip"), {}, [this] { editSelected(tr("Mute"), [](Clip &c) { c.fx.mute = !c.fx.mute; }); });
    add("normalize", tr("&Normalize loudness"), {}, [this] { editSelected(tr("Normalize"), [](Clip &c) { c.fx.normalize = !c.fx.normalize; }); });
    add("fade1", tr("Fade in and out (1 s)"), {}, [this] { editSelected(tr("Fade"), [](Clip &c) { c.fx.fadeIn = 1; c.fx.fadeOut = 1; }); });
    add("silence", tr("Remove &silence…"), {}, [this] { removeSilence(); });
    add("record", tr("&Record voice-over…"), QKeySequence("Ctrl+Shift+M"), [this] {
        if (m_prev) m_prev->pause();
        RecordDialog dlg(AppSettings::dataDir() + "/recordings", this);
        if (dlg.exec() == QDialog::Accepted && !dlg.recordedFile().isEmpty()) {
            ProbeResult r = probeBlocking(dlg.recordedFile());
            if (!r.ok) { notify(r.error, true); return; }
            const MediaItem mi = r.item;
            m->mutate(tr("Add recording"), [mi](Project &p) { p.media.push_back(mi); });
            requestAssets(mi);
            const QString tr2 = m->project().firstTrackOfType(TrackType::Voice);
            addClipFromMedia(mi.id, tr2, m->playhead());
            notify(tr("Voice-over recorded and added at the playhead."));
        }
    });

    add("fx_reset", tr("Reset all effects of the clip"), {}, [this] {
        editSelected(tr("Reset effects"), [](Clip &c) { const double sp = c.fx.speed; c.fx = ClipEffects(); c.fx.speed = sp; });
    });
    add("fx_pic", tr("Picture effects…"), {}, [this] { m_dockProps->show(); m_dockProps->raise(); });
    add("fx_aud", tr("Audio effects…"), {}, [this] { m_dockProps->show(); m_dockProps->raise(); });

    add("batch_a", tr("Batch &audio conversion…"), {}, [this] { BatchDialog d(true, AppSettings::exportDir(), this); d.exec(); });
    add("batch_v", tr("Batch &video conversion…"), {}, [this] { BatchDialog d(false, AppSettings::exportDir(), this); d.exec(); });
    add("info", tr("Media &information…"), {}, [this] {
        QString id = m->selectedMedia();
        if (id.isEmpty()) if (const Clip *c = m->project().clipById(m->selectedClip())) id = c->mediaId;
        const MediaItem *mi = m->project().mediaById(id);
        if (!mi) { notify(tr("Select a media item or a clip first.")); return; }
        ProbeResult r = probeBlocking(mi->path);
        MediaInfoDialog(r.ok ? r.item : *mi, r.rawJson, this).exec();
    });
    add("proxy", tr("Create optimized &preview (proxy)"), {}, [this] { createProxy(m->selectedMedia()); });
    add("clearcache", tr("Clear cache"), {}, [this] {
        const qint64 n = WaveformCache::clearAll();
        notify(tr("Cache cleared (%1 freed).").arg(humanSize(n)));
    });
    add("settings", tr("&Settings…"), QKeySequence("Ctrl+,"), [this] {
        SettingsDialog d(this);
        if (d.exec() == QDialog::Accepted) {
            Theme::apply(AppSettings::darkTheme()); m_tl->viewport()->update();
            m_autosave.stop(); if (AppSettings::autosaveMinutes() > 0) m_autosave.start(AppSettings::autosaveMinutes() * 60000);
        }
    });

    add("zoomin", tr("Zoom &in"), QKeySequence::ZoomIn, [this] { m_tl->zoomBy(1.4); });
    add("zoomout", tr("Zoom &out"), QKeySequence::ZoomOut, [this] { m_tl->zoomBy(1 / 1.4); });
    add("zoomfit", tr("Zoom to &fit"), QKeySequence("Ctrl+0"), [this] { m_tl->zoomFit(); });
    add("dark", tr("&Dark theme"), {}, [this] { AppSettings::setDarkTheme(true); Theme::apply(true); m_tl->viewport()->update(); });
    add("light", tr("&Light theme"), {}, [this] { AppSettings::setDarkTheme(false); Theme::apply(false); m_tl->viewport()->update(); });
    add("proxies", tr("Use proxies for preview"), {}, [this] { AppSettings::setUseProxies(a["proxies"]->isChecked()); });
    a["proxies"]->setCheckable(true); a["proxies"]->setChecked(AppSettings::useProxies());
    add("resetlayout", tr("Reset window layout"), {}, [this] {
        AppSettings::s().remove("ui/state"); AppSettings::s().remove("ui/geometry");
        QMessageBox::information(this, tr("Layout"), tr("The default layout will be used the next time you start M Veditor."));
    });

    add("play", tr("Play / Pause"), QKeySequence(Qt::Key_Space), [this] { m_prev->togglePlay(); });
    add("prevframe", tr("Previous frame"), QKeySequence(Qt::Key_Left), [this] { m_prev->stepFrame(-1); });
    add("nextframe", tr("Next frame"), QKeySequence(Qt::Key_Right), [this] { m_prev->stepFrame(1); });
    add("home", tr("Go to start"), QKeySequence(Qt::Key_Home), [this] { m->setPlayhead(0); });
    add("end", tr("Go to end"), QKeySequence(Qt::Key_End), [this] { m->setPlayhead(m->project().duration()); });
    add("prevedge", tr("Previous clip edge"), QKeySequence(Qt::Key_Up), [this] { jumpEdge(-1); });
    add("nextedge", tr("Next clip edge"), QKeySequence(Qt::Key_Down), [this] { jumpEdge(1); });

    add("exportv", tr("Export &video…"), QKeySequence("Ctrl+E"), [this] { m_dockExp->show(); m_dockExp->raise(); m_exp->selectFormat("mp4-h264"); }, tr("Open the export panel with a video format"));
    add("exporta", tr("Export &audio (MP3)…"), QKeySequence("Ctrl+Shift+E"), [this] { m_dockExp->show(); m_dockExp->raise(); m_exp->selectFormat("mp3"); });
    add("exportnow", tr("Export now"), {}, [this] { startExport(); });
    add("preview", tr("&Render preview of the timeline"), QKeySequence("F5"), [this] { renderPreview(); });

    add("manual", tr("&User manual"), QKeySequence::HelpContents, [this] { showManual(); });
    add("licenses", tr("&Licenses and bundled components"), {}, [this] {
        QFile f(":/docs/LICENSES.md"); f.open(QIODevice::ReadOnly);
        TextViewer(tr("Licenses"), QString::fromUtf8(f.readAll()), this).exec();
    });
    add("log", tr("Show activity &log"), {}, [this] { m_dockLog->show(); });
    add("about", tr("&About M Veditor"), {}, [this] { about(); });
}

void MainWindow::buildMenus() {
    auto *file = menuBar()->addMenu(tr("&File"));
    file->addAction(a["new"]); file->addAction(a["open"]);
    m_recentMenu = file->addMenu(tr("Open &recent"));
    file->addSeparator(); file->addAction(a["save"]); file->addAction(a["saveas"]);
    file->addSeparator(); file->addAction(a["import"]); file->addAction(a["relink"]);
    file->addSeparator(); file->addAction(a["exit"]);
    connect(file, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);

    auto *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m->undoStack()->createUndoAction(this, tr("&Undo")));
    edit->addAction(m->undoStack()->createRedoAction(this, tr("&Redo")));
    auto *undo = edit->actions().at(0); undo->setShortcut(QKeySequence::Undo);
    auto *redo = edit->actions().at(1); redo->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Y")});
    edit->addSeparator();
    for (auto *k : {"cut", "copy", "paste", "dup"}) edit->addAction(a[k]);
    edit->addSeparator();
    for (auto *k : {"split", "delete", "ripple"}) edit->addAction(a[k]);
    edit->addSeparator(); edit->addAction(a["marker"]); edit->addAction(a["snap"]);

    auto *video = menuBar()->addMenu(tr("&Video"));
    for (auto *k : {"speed", "reverse", "rot", "fliph", "flipv"}) video->addAction(a[k]);
    video->addSeparator();
    for (auto *k : {"title", "subtitle", "caption", "watermark", "pip"}) video->addAction(a[k]);
    video->addSeparator(); video->addAction(a["detach"]);
    video->addSeparator();
    auto *tracks = video->addMenu(tr("Add track"));
    for (auto *k : {"track_v", "track_a", "track_m", "track_vo", "track_t"}) tracks->addAction(a[k]);

    auto *audio = menuBar()->addMenu(tr("&Audio"));
    for (auto *k : {"extract", "detach", "mute", "normalize", "fade1", "silence", "record"}) audio->addAction(a[k]);

    auto *fx = menuBar()->addMenu(tr("E&ffects"));
    m_transMenu = fx->addMenu(tr("Transition in"));
    const QList<QPair<QString, QString>> trs = {{"none", tr("None")}, {"fade", tr("Fade through black")}, {"dissolve", tr("Cross dissolve")},
        {"wipeleft", tr("Wipe left to right")}, {"wiperight", tr("Wipe right to left")}, {"wipeup", tr("Wipe top to bottom")}, {"wipedown", tr("Wipe bottom to top")}};
    for (auto &t : trs) m_transMenu->addAction(t.second, this, [this, id = t.first] { setTransition(id); });
    fx->addSeparator(); fx->addAction(a["fx_pic"]); fx->addAction(a["fx_aud"]); fx->addAction(a["fx_reset"]);

    auto *tools = menuBar()->addMenu(tr("&Tools"));
    for (auto *k : {"batch_a", "batch_v", "info", "proxy", "clearcache", "settings"}) tools->addAction(a[k]);

    auto *view = menuBar()->addMenu(tr("Vie&w"));
    for (auto *k : {"zoomin", "zoomout", "zoomfit"}) view->addAction(a[k]);
    view->addSeparator();
    auto *themeGroup = new QActionGroup(this);
    for (auto *k : {"dark", "light"}) { a[k]->setCheckable(true); themeGroup->addAction(a[k]); view->addAction(a[k]); }
    a[AppSettings::darkTheme() ? "dark" : "light"]->setChecked(true);
    view->addSeparator(); view->addAction(a["proxies"]);
    view->addSeparator();
    for (auto *d : {m_dockLib, m_dockProps, m_dockExp, m_dockLog}) view->addAction(d->toggleViewAction());
    view->addSeparator(); view->addAction(a["resetlayout"]);
    view->addSeparator();
    for (auto *k : {"play", "prevframe", "nextframe", "home", "end", "prevedge", "nextedge"}) view->addAction(a[k]);

    auto *ex = menuBar()->addMenu(tr("E&xport"));
    for (auto *k : {"exportv", "exporta", "exportnow", "preview", "extract"}) ex->addAction(a[k]);
    ex->addSeparator(); ex->addAction(a["batch_a"]); ex->addAction(a["batch_v"]);

    auto *help = menuBar()->addMenu(tr("&Help"));
    for (auto *k : {"manual", "licenses", "log", "about"}) help->addAction(a[k]);
}

void MainWindow::buildToolbar() {
    auto *tb = addToolBar(tr("Tools")); tb->setObjectName("mainToolbar"); tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto put = [&](const char *id, const QString &txt) { a[id]->setIconText(txt); tb->addAction(a[id]); };
    put("import", tr("＋ Import")); tb->addSeparator();
    tb->addAction(menuBar()->actions().at(1)->menu()->actions().at(0)); // Undo
    tb->addAction(menuBar()->actions().at(1)->menu()->actions().at(1)); // Redo
    tb->addSeparator();
    put("split", tr("✂ Split")); put("delete", tr("✖ Delete")); put("dup", tr("⧉ Duplicate")); tb->addSeparator();
    put("speed", tr("⏱ Speed")); put("title", tr("T Title")); put("detach", tr("♪ Detach audio")); put("record", tr("● Record")); tb->addSeparator();
    put("marker", tr("⚑ Marker")); tb->addAction(a["snap"]); a["snap"]->setIconText(tr("🧲 Snap")); tb->addSeparator();
    put("zoomout", tr("－")); put("zoomin", tr("＋")); put("zoomfit", tr("Fit")); tb->addSeparator();
    put("preview", tr("▶ Render preview")); put("exportnow", tr("⬆ Export"));
}

void MainWindow::updateActions() {
    const Clip *c = m->project().clipById(m->selectedClip());
    const bool has = c != nullptr;
    const MediaItem *mi = has && !c->isTitle() ? m->project().mediaById(c->mediaId) : nullptr;
    const bool videoish = has && mi && mi->hasVideo;
    for (auto *k : {"copy", "cut", "dup", "delete", "ripple", "fx_reset"}) a[k]->setEnabled(has);
    for (auto *k : {"speed", "reverse"}) a[k]->setEnabled(has && !c->isTitle());
    for (auto *k : {"rot", "fliph", "flipv", "pip"}) a[k]->setEnabled(videoish);
    for (auto *k : {"detach", "mute", "normalize", "silence", "fade1"}) a[k]->setEnabled(has && mi && mi->hasAudio);
    a["paste"]->setEnabled(!m_clipboard.isEmpty());
    m_transMenu->setEnabled(videoish);
    a["split"]->setEnabled(!m->project().clips.isEmpty());
}

// ======================================================================== project
bool MainWindow::maybeSave() {
    if (!m->isDirty()) return true;
    if (m->project().clips.isEmpty() && m->project().media.isEmpty()) return true;
    const auto r = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to \"%1\" before continuing?").arg(m->project().name),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return saveProject();
    return true;
}

void MainWindow::newProject() {
    if (busy()) { QMessageBox::information(this, tr("Busy"), tr("Please wait for the current render to finish or cancel it first.")); return; }
    if (!maybeSave()) return;
    m_prev->releaseMedia();
    Project p; p.ensureDefaultTracks();
    m->reset(p, {});
    clearRecovery();
    notify(tr("New project created."));
}

bool MainWindow::writeProject(const QString &path) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { notify(tr("Cannot write the project file: %1").arg(f.errorString()), true); return false; }
    f.write(QJsonDocument(m->project().toJson(path)).toJson(QJsonDocument::Indented));
    if (!f.commit()) { notify(tr("Saving failed: %1").arg(f.errorString()), true); return false; }
    return true;
}

bool MainWindow::saveProject() {
    if (m->filePath().isEmpty()) return saveProjectAs();
    if (!writeProject(m->filePath())) return false;
    m->setDirty(false); clearRecovery();
    notify(tr("Project saved."));
    return true;
}
bool MainWindow::saveProjectAs() {
    QString dir = m->filePath().isEmpty() ? AppSettings::exportDir() : QFileInfo(m->filePath()).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, tr("Save project"), QDir(dir).absoluteFilePath(m->project().name + ".mveproj"), tr("M Veditor project (*.mveproj)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(".mveproj", Qt::CaseInsensitive)) path += ".mveproj";
    if (!writeProject(path)) return false;
    m->setFilePath(path); m->setDirty(false); AppSettings::addRecent(path); clearRecovery();
    notify(tr("Project saved."));
    return true;
}
void MainWindow::openDialog() {
    if (!maybeSave()) return;
    const QString p = QFileDialog::getOpenFileName(this, tr("Open project"), AppSettings::exportDir(), tr("M Veditor project (*.mveproj);;All files (*)"));
    if (!p.isEmpty()) openProject(p);
}
bool MainWindow::openProject(const QString &path) {
    if (busy()) { QMessageBox::information(this, tr("Busy"), tr("Please wait for the current render to finish or cancel it first.")); return false; }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, tr("Open project"), tr("The file could not be opened:\n%1").arg(f.errorString())); AppSettings::removeRecent(path); return false; }
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) { QMessageBox::warning(this, tr("Open project"), tr("This project file is damaged and cannot be read.")); return false; }
    QString err;
    Project p = Project::fromJson(doc.object(), path, &err);
    if (!err.isEmpty()) { QMessageBox::warning(this, tr("Open project"), err); return false; }
    m_prev->releaseMedia();
    m->reset(p, path);
    AppSettings::addRecent(path);
    for (const auto &mi : p.media) if (!mi.missing) requestAssets(mi);
    int missing = 0; for (auto &mi : p.media) if (mi.missing) ++missing;
    if (missing) {
        notify(tr("%1 media file(s) are missing.").arg(missing), true);
        if (QMessageBox::question(this, tr("Missing media"), tr("%1 media file(s) could not be found. Do you want to locate them now?").arg(missing)) == QMessageBox::Yes) relinkMissing();
    }
    m_tl->zoomFit();
    return true;
}
void MainWindow::rebuildRecentMenu() {
    m_recentMenu->clear();
    for (const QString &p : AppSettings::recents()) {
        if (!QFileInfo::exists(p)) continue;
        m_recentMenu->addAction(QDir::toNativeSeparators(p), this, [this, p] { if (maybeSave()) openProject(p); });
    }
    m_recentMenu->setEnabled(!m_recentMenu->isEmpty());
}

void MainWindow::relinkMissing() {
    QVector<QString> missing;
    for (auto &mi : m->project().media) if (mi.missing) missing << mi.id;
    if (missing.isEmpty()) { QMessageBox::information(this, tr("Relink"), tr("No media is missing.")); return; }
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder to search for the missing files"), AppSettings::importDir());
    QHash<QString, QString> found;
    if (!dir.isEmpty()) {
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            for (const QString &id : missing) {
                const MediaItem *mi = m->project().mediaById(id);
                if (mi && !found.contains(id) && it.fileName().compare(mi->name, Qt::CaseInsensitive) == 0) found[id] = it.filePath();
            }
        }
    }
    QStringList still;
    for (const QString &id : missing) if (!found.contains(id)) still << id;
    if (!still.isEmpty() && QMessageBox::question(this, tr("Relink"), tr("%1 file(s) were not found automatically. Locate them one by one?").arg(still.size())) == QMessageBox::Yes)
        for (const QString &id : still) {
            const MediaItem *mi = m->project().mediaById(id);
            const QString p = QFileDialog::getOpenFileName(this, tr("Locate %1").arg(mi->name), dir, tr(kMediaFilter));
            if (!p.isEmpty()) found[id] = p;
        }
    int n = 0;
    m->mutate(tr("Relink media"), [&](Project &p) {
        for (auto it = found.begin(); it != found.end(); ++it) if (MediaItem *mi = p.mediaById(it.key())) { mi->path = QFileInfo(it.value()).absoluteFilePath(); mi->missing = false; ++n; }
    });
    for (auto &mi : m->project().media) if (!mi.missing) requestAssets(mi);
    notify(tr("%1 file(s) relinked.").arg(n));
}

void MainWindow::requestAssets(const MediaItem &mi) {
    if (mi.hasVideo || mi.kind == "image") m_thumbs->request(mi.id, mi.path, mi.kind == "image" ? 0 : std::min(1.0, mi.duration * 0.1), mi.kind == "image");
    if (mi.hasAudio && mi.kind != "image" && !m->waveform(mi.id)) {
        auto *w = new WaveformLoader(this);
        const QString path = mi.path;
        connect(w, &WaveformLoader::ready, this, [this, w](const QString &p, const QVector<float> &peaks) { m->setWaveform(p, peaks); w->deleteLater(); });
        connect(w, &WaveformLoader::failed, w, &QObject::deleteLater);
        w->start(path);
    }
}

// ---- autosave / crash recovery
static QString autosavePath() { return AppSettings::recoveryDir() + "/autosave.mveproj"; }
void MainWindow::autosave() {
    if (!m_primaryInstance || !m->isDirty() || (m->project().clips.isEmpty() && m->project().media.isEmpty())) return;
    QJsonObject o = m->project().toJson(autosavePath());
    o["_originalPath"] = m->filePath();
    QSaveFile f(autosavePath());
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)); f.commit(); statusBar()->showMessage(tr("Autosaved."), 2000); }
}
void MainWindow::clearRecovery() { if (m_primaryInstance) QFile::remove(autosavePath()); }
void MainWindow::checkRecovery() {
    if (!m_primaryInstance || !QFileInfo::exists(autosavePath())) return;
    QFile f(autosavePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QString err;
    Project p = Project::fromJson(o, autosavePath(), &err);
    if (!err.isEmpty() || (p.clips.isEmpty() && p.media.isEmpty())) { QFile::remove(autosavePath()); return; }
    if (QMessageBox::question(this, tr("Recover project"), tr("M Veditor did not close properly last time. An automatic backup of \"%1\" was found.\n\nDo you want to recover it?").arg(p.name)) != QMessageBox::Yes) {
        QFile::remove(autosavePath()); return;
    }
    m->reset(p, o["_originalPath"].toString());
    m->setDirty(true);
    for (const auto &mi : p.media) if (!mi.missing) requestAssets(mi);
    m_tl->zoomFit();
    notify(tr("Project recovered from the automatic backup. Please save it."));
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (busy()) {
        if (QMessageBox::question(this, tr("Render in progress"), tr("A render is still running. Cancel it and exit?")) != QMessageBox::Yes) { e->ignore(); return; }
        m_job->cancel();
    }
    if (!maybeSave()) { e->ignore(); return; }
    m_prev->releaseMedia();
    AppSettings::s().setValue("ui/geometry", saveGeometry());
    AppSettings::s().setValue("ui/state", saveState());
    if (auto *sp = findChild<QSplitter *>("centerSplit")) AppSettings::s().setValue("ui/split", sp->saveState());
    clearRecovery();
    if (!m_previewFile.isEmpty()) QFile::remove(m_previewFile);
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void MainWindow::dropEvent(QDropEvent *e) {
    QStringList files;
    for (auto &u : e->mimeData()->urls()) if (u.isLocalFile()) files << u.toLocalFile();
    if (files.size() == 1 && files.first().endsWith(".mveproj", Qt::CaseInsensitive)) { if (maybeSave()) openProject(files.first()); return; }
    if (!files.isEmpty()) importFiles(files);
}

// ======================================================================== import
void MainWindow::importFiles(const QStringList &files) {
    QString why;
    if (!tools::available(&why)) { QMessageBox::critical(this, tr("FFmpeg missing"), why); return; }
    for (const QString &f : files) {
        const QString abs = QFileInfo(f).absoluteFilePath();
        bool dup = false;
        for (auto &mi : m->project().media) if (QFileInfo(mi.path).absoluteFilePath().compare(abs, Qt::CaseInsensitive) == 0) { dup = true; m->selectMedia(mi.id); }
        if (dup) { notify(tr("\"%1\" is already in the project.").arg(QFileInfo(f).fileName())); continue; }
        auto *task = new ProbeTask(this);
        ++m_pendingImports;
        connect(task, &ProbeTask::finished, this, [this, task, abs](const ProbeResult &r) { task->deleteLater(); onProbed(abs, r); });
        task->start(abs);
    }
}

void MainWindow::onProbed(const QString &path, const ProbeResult &r) {
    --m_pendingImports;
    if (!r.ok) m_importErrors << QFileInfo(path).fileName() + ": " + r.error;
    else {
        const MediaItem mi = r.item;
        m->mutate(tr("Import media"), [mi](Project &p) {
            bool hadVideo = false; for (auto &x : p.media) if (x.hasVideo && x.kind == "video") hadVideo = true;
            p.media.push_back(mi);
            if (!hadVideo && mi.kind == "video") {                 // first video decides the project format
                p.settings.width = std::max(2, mi.width & ~1); p.settings.height = std::max(2, mi.height & ~1);
                p.settings.fps = mi.fps >= 5 && mi.fps <= 144 ? mi.fps : 30;
            }
        });
        requestAssets(mi);
        notify(tr("Imported %1").arg(mi.name));
    }
    if (m_pendingImports == 0 && !m_importErrors.isEmpty()) {
        QMessageBox::warning(this, tr("Some files could not be imported"), m_importErrors.join("\n\n"));
        for (auto &e : m_importErrors) m_log->appendPlainText("ERROR: " + e);
        m_importErrors.clear();
    }
}

void MainWindow::removeMedia(const QString &id) {
    const MediaItem *mi = m->project().mediaById(id);
    if (!mi) return;
    const int uses = m->project().usageCount(id);
    QString text = tr("Remove \"%1\" from the project?\nThe file on your disk is not deleted.").arg(mi->name);
    if (uses) text += "\n\n" + tr("It is used by %1 clip(s) on the timeline. Those clips will be removed too.").arg(uses);
    if (QMessageBox::question(this, tr("Remove media"), text) != QMessageBox::Yes) return;
    if (m->selectedMedia() == id) m->selectMedia({});
    m->mutate(tr("Remove media"), [id](Project &p) {
        p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &c) { return c.mediaId == id; }), p.clips.end());
        p.media.erase(std::remove_if(p.media.begin(), p.media.end(), [&](const MediaItem &x) { return x.id == id; }), p.media.end());
    });
}
void MainWindow::renameMedia(const QString &id) {
    const MediaItem *mi = m->project().mediaById(id);
    if (!mi) return;
    bool ok; const QString n = QInputDialog::getText(this, tr("Rename"), tr("Name in the project:"), QLineEdit::Normal, mi->name, &ok);
    if (ok && !n.trimmed().isEmpty()) m->mutate(tr("Rename media"), [id, n](Project &p) { if (auto *x = p.mediaById(id)) x->name = n.trimmed(); });
}

// ======================================================================== clip editing
QString MainWindow::defaultTrackFor(const MediaItem &mi, bool audioOnly) const {
    const Project &p = m->project();
    if (mi.kind == "audio" || audioOnly) return p.firstTrackOfType(TrackType::Audio);
    for (int i = p.tracks.size() - 1; i >= 0; --i) if (p.tracks[i].type == TrackType::Video) return p.tracks[i].id;   // lowest video track
    return {};
}

void MainWindow::addClipFromMedia(const QString &mediaId, const QString &trackId, double start, double in, double out) {
    const MediaItem *mi = m->project().mediaById(mediaId);
    if (!mi) return;
    if (mi->missing) { notify(tr("\"%1\" is missing. Use File > Relink Missing Media.").arg(mi->name), true); return; }
    QString tid = trackId;
    if (tid.isEmpty()) tid = defaultTrackFor(*mi);
    if (!m->project().trackById(tid)) { notify(tr("There is no suitable track. Add one from Video > Add track."), true); return; }
    const double o = out < 0 ? (mi->kind == "image" ? kImageSeconds : mi->duration) : out;
    Clip c; c.id = newId("c"); c.mediaId = mediaId; c.trackId = tid; c.in = std::max(0.0, in); c.out = std::max(c.in + 0.05, o);
    const double dur = c.out - c.in;
    c.start = m->project().resolveOverlap(tid, start, dur);
    const QString id = c.id;
    m->mutate(tr("Add clip"), [c](Project &p) { p.clips.push_back(c); });
    m->selectClip(id);
}

void MainWindow::editSelected(const QString &what, const std::function<void(Clip &)> &fn) {
    const QString id = m->selectedClip();
    if (id.isEmpty()) { notify(tr("Select a clip on the timeline first.")); return; }
    m->mutate(what, [id, fn](Project &p) { if (Clip *c = p.clipById(id)) fn(*c); });
}
void MainWindow::setTransition(const QString &t) {
    editSelected(tr("Set transition"), [t](Clip &c) { c.fx.transition = t; if (t != "none" && c.fx.transitionDur <= 0) c.fx.transitionDur = 1.0; });
    if (t != "none") notify(tr("Tip: place the clip directly after the previous one on the same track for a cross-transition."));
}

void MainWindow::splitAtPlayhead() {
    const double t = m->playhead();
    const Project &p = m->project();
    QStringList targets;
    const Clip *sel = p.clipById(m->selectedClip());
    auto inside = [&](const Clip &c) { return t > c.start + 0.02 && t < c.end() - 0.02; };
    if (sel && inside(*sel)) targets << sel->id;
    else for (auto &c : p.clips) if (inside(c) && p.trackById(c.trackId) && !p.trackById(c.trackId)->locked) targets << c.id;
    if (targets.isEmpty()) { notify(tr("Move the playhead over a clip to split it.")); return; }
    m->mutate(tr("Split clip"), [targets, t](Project &pr) {
        for (const QString &id : targets) {
            Clip *c = pr.clipById(id);
            if (!c) continue;
            const double srcT = c->in + (t - c->start) * c->fx.speed;
            Clip right = *c; right.id = newId("c"); right.start = t; right.in = srcT;
            right.fx.fadeIn = 0; right.fx.transition = "none";
            c->out = srcT; c->fx.fadeOut = 0;
            pr.clips.push_back(right);
        }
    });
    notify(tr("Split at %1").arg(humanTime(t, true)));
}

void MainWindow::deleteSelected(bool ripple) {
    const QString id = m->selectedClip();
    const Clip *c = m->project().clipById(id);
    if (!c) { notify(tr("Select a clip first.")); return; }
    const QString track = c->trackId; const double s = c->start, d = c->duration();
    m->mutate(tr("Delete clip"), [=](Project &p) {
        p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &x) { return x.id == id; }), p.clips.end());
        if (ripple) for (Clip &x : p.clips) if (x.trackId == track && x.start >= s + d - 1e-6) x.start = std::max(0.0, x.start - d);
    });
    m->selectClip({});
}
void MainWindow::duplicateSelected() {
    const Clip *c = m->project().clipById(m->selectedClip());
    if (!c) return;
    Clip n = *c; n.id = newId("c");
    n.start = m->project().resolveOverlap(c->trackId, c->end(), c->duration());
    const QString id = n.id;
    m->mutate(tr("Duplicate clip"), [n](Project &p) { p.clips.push_back(n); });
    m->selectClip(id);
}
void MainWindow::copySelected(bool cut) {
    const Clip *c = m->project().clipById(m->selectedClip());
    if (!c) return;
    m_clipboard = {*c};
    updateActions();
    if (cut) deleteSelected(false); else notify(tr("Clip copied."));
}
void MainWindow::pasteClip() {
    if (m_clipboard.isEmpty()) return;
    Clip n = m_clipboard.first(); n.id = newId("c");
    const Project &p = m->project();
    const MediaItem *mi = n.isTitle() ? nullptr : p.mediaById(n.mediaId);
    if (!n.isTitle() && !mi) { notify(tr("The copied clip's media is no longer in the project."), true); return; }
    const Track *tr0 = p.trackById(n.trackId);
    if (!tr0 || !TimelineView::compatible(n, mi, *tr0)) {
        n.trackId.clear();
        for (auto &t : p.tracks) if (TimelineView::compatible(n, mi, t)) { n.trackId = t.id; break; }
        if (n.trackId.isEmpty()) { notify(tr("There is no suitable track to paste into."), true); return; }
    }
    n.start = p.resolveOverlap(n.trackId, m->playhead(), n.duration());
    const QString id = n.id;
    m->mutate(tr("Paste clip"), [n](Project &pr) { pr.clips.push_back(n); });
    m->selectClip(id);
}

void MainWindow::detachAudio() {
    const Clip *c = m->project().clipById(m->selectedClip());
    if (!c || c->isTitle()) { notify(tr("Select a video clip first.")); return; }
    const MediaItem *mi = m->project().mediaById(c->mediaId);
    if (!mi || !mi->hasAudio || mi->kind == "audio") { notify(tr("This clip has no audio that can be detached.")); return; }
    const QString id = c->id;
    Clip a2; a2.id = newId("c"); a2.mediaId = c->mediaId; a2.in = c->in; a2.out = c->out; a2.start = c->start;
    a2.fx.speed = c->fx.speed; a2.fx.preservePitch = c->fx.preservePitch; a2.fx.volume = c->fx.volume; a2.fx.reverse = c->fx.reverse;
    a2.trackId = m->project().firstTrackOfType(TrackType::Audio);
    if (a2.trackId.isEmpty()) { notify(tr("There is no audio track. Add one first."), true); return; }
    a2.start = m->project().resolveOverlap(a2.trackId, c->start, c->duration());
    m->mutate(tr("Detach audio"), [id, a2](Project &p) { if (Clip *x = p.clipById(id)) x->fx.mute = true; p.clips.push_back(a2); });
    notify(tr("Audio detached to the audio track."));
}

void MainWindow::applySpeedDialog() {
    const Clip *c = m->project().clipById(m->selectedClip());
    if (!c || c->isTitle()) { notify(tr("Select a clip on the timeline first.")); return; }
    const MediaItem *mi = m->project().mediaById(c->mediaId);
    if (mi && mi->kind == "image") { notify(tr("Still images don't have a speed. Drag the clip's end to change how long it shows.")); return; }
    SpeedDialog dlg(c->fx.speed, c->out - c->in, mi && mi->hasAudio, c->fx.preservePitch, c->fx.reverse, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const double s = dlg.speed(); const bool pp = dlg.preservePitch(), rev = dlg.reverse(); const auto mode = dlg.audioMode();
    const QString id = c->id;
    Clip a2;
    if (mode == SpeedDialog::DetachAudio) {
        a2.id = newId("c"); a2.mediaId = c->mediaId; a2.in = c->in; a2.out = c->out; a2.trackId = m->project().firstTrackOfType(TrackType::Audio);
        a2.fx.volume = c->fx.volume;
        if (a2.trackId.isEmpty()) { notify(tr("There is no audio track to detach to."), true); return; }
        a2.start = m->project().resolveOverlap(a2.trackId, c->start, c->duration());
    }
    m->mutate(tr("Change speed"), [=](Project &p) {
        Clip *x = p.clipById(id); if (!x) return;
        x->fx.speed = s; x->fx.preservePitch = pp; x->fx.reverse = rev;
        if (mode == SpeedDialog::MuteAudio || mode == SpeedDialog::DetachAudio) x->fx.mute = true;
        if (mode == SpeedDialog::KeepAudio) x->fx.mute = false;
        if (mode == SpeedDialog::DetachAudio) p.clips.push_back(a2);
        // keep the track tidy: push following clips if the new length overlaps them
        double end = x->end();
        for (const Clip *o : p.clipsOnTrack(x->trackId)) {
            Clip *oc = p.clipById(o->id);
            if (oc->id == id || oc->start < x->start) continue;
            if (oc->start < end - 1e-6) oc->start = end;
            end = oc->end();
        }
    });
    notify(tr("Speed set to %1× – the clip is now %2 long.").arg(QString::number(s, 'g', 4), humanTime(m->project().clipById(id)->duration(), true)));
}

void MainWindow::addTitle(const QString &kind) {
    const Project &p = m->project();
    QString tid = p.firstTrackOfType(TrackType::Title);
    if (tid.isEmpty()) { notify(tr("There is no title track. Add one from Video > Add track."), true); return; }
    Clip c; c.id = newId("c"); c.trackId = tid; c.in = 0; c.out = 4;
    c.title.kind = kind;
    if (kind == "title") { c.title.text = tr("Your title"); c.title.fontSize = 96; c.title.posY = 0.45; }
    else if (kind == "subtitle") { c.title.text = tr("Subtitle text"); c.title.fontSize = 52; c.title.posY = 0.85; }
    else { c.title.text = tr("Caption"); c.title.fontSize = 40; c.title.posY = 0.92; c.title.boxOpacity = 0.6; }
    c.fx.fadeIn = 0.3; c.fx.fadeOut = 0.3;
    c.start = p.resolveOverlap(tid, m->playhead(), c.out - c.in);
    const QString id = c.id;
    m->mutate(tr("Add %1").arg(kind), [c](Project &pr) { pr.clips.push_back(c); });
    m->selectClip(id);
    m_dockProps->show(); m_dockProps->raise();
}

void MainWindow::addWatermark() {
    const QString f = QFileDialog::getOpenFileName(this, tr("Choose a logo or watermark image"), AppSettings::importDir(), tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (f.isEmpty()) return;
    ProbeResult r = probeBlocking(f);
    if (!r.ok) { notify(r.error, true); return; }
    MediaItem mi = r.item;
    for (auto &x : m->project().media) if (QFileInfo(x.path) == QFileInfo(f)) mi = x;
    const bool isNew = !m->project().mediaById(mi.id);
    QString tid;
    for (auto &t : m->project().tracks) if (t.type == TrackType::Video) { tid = t.id; break; }   // topmost video track
    Clip c; c.id = newId("c"); c.mediaId = mi.id; c.trackId = tid; c.in = 0;
    c.out = std::max(kImageSeconds, m->project().duration());
    c.fx.scale = 0.15; c.fx.posX = 0.97; c.fx.posY = 0.05; c.fx.opacity = 0.75;
    c.start = m->project().resolveOverlap(tid, 0, c.out);
    const QString id = c.id;
    m->mutate(tr("Add watermark"), [=](Project &p) { if (isNew) p.media.push_back(mi); p.clips.push_back(c); });
    if (isNew) requestAssets(mi);
    m->selectClip(id);
    notify(tr("Watermark added to the top video track. Adjust size, position and opacity in Properties."));
}

void MainWindow::applyPip() {
    const Clip *c = m->project().clipById(m->selectedClip());
    if (!c) return;
    QString top;
    for (auto &t : m->project().tracks) if (t.type == TrackType::Video) { top = t.id; break; }
    const QString id = c->id;
    const double dur = c->duration();
    m->mutate(tr("Picture-in-picture"), [=](Project &p) {
        Clip *x = p.clipById(id); if (!x) return;
        x->fx.scale = 0.3; x->fx.posX = 0.97; x->fx.posY = 0.97;
        if (!top.isEmpty() && x->trackId != top) { x->trackId = top; x->start = p.resolveOverlap(top, x->start, dur, id); }
    });
    notify(tr("Picture-in-picture applied. Place it over other footage and adjust position in Properties."));
}

void MainWindow::removeSilence() {
    const Clip *c = m->project().clipById(m->selectedClip());
    if (!c || c->isTitle()) { notify(tr("Select an audio or video clip first.")); return; }
    const MediaItem *mi = m->project().mediaById(c->mediaId);
    if (!mi || !mi->hasAudio) { notify(tr("This clip has no audio.")); return; }
    bool ok = false;
    const double db = QInputDialog::getDouble(this, tr("Remove silence"), tr("Silence threshold (dB). Lower = only very quiet parts:"), -40, -90, -10, 1, &ok);
    if (!ok) return;
    const double minS = QInputDialog::getDouble(this, tr("Remove silence"), tr("Minimum silence length (seconds):"), 0.5, 0.05, 10, 2, &ok);
    if (!ok) return;
    const QString id = c->id;
    notify(tr("Analyzing silence…"));
    auto *det = new SilenceDetector(this);
    connect(det, &SilenceDetector::finished, this, [this, det, id](const QVector<TimeRange> &keep, bool good) {
        det->deleteLater();
        const Clip *cc = m->project().clipById(id);
        if (!good || !cc) { notify(tr("Silence analysis failed."), true); return; }
        // intersect with the clip's source range
        QVector<TimeRange> segs;
        for (auto &k : keep) { const double a = std::max(k.start, cc->in), b = std::min(k.end, cc->out); if (b - a > 0.05) segs.push_back({a, b}); }
        if (segs.isEmpty()) { notify(tr("Nothing but silence was found; the clip was left unchanged.")); return; }
        if (segs.size() == 1 && segs[0].start <= cc->in + 0.01 && segs[0].end >= cc->out - 0.01) { notify(tr("No silence found with these settings.")); return; }
        const double oldDur = cc->duration();
        m->mutate(tr("Remove silence"), [=](Project &p) {
            Clip *x = p.clipById(id); if (!x) return;
            const Clip base = *x; double pos = base.start; double newTotal = 0;
            QVector<Clip> add;
            for (int i = 0; i < segs.size(); ++i) {
                Clip n = base; n.id = i == 0 ? base.id : newId("c"); n.in = segs[i].start; n.out = segs[i].end; n.start = pos;
                n.fx.fadeIn = i == 0 ? base.fx.fadeIn : 0; n.fx.fadeOut = i == segs.size() - 1 ? base.fx.fadeOut : 0;
                pos += n.duration(); newTotal += n.duration(); add.push_back(n);
            }
            *x = add.first();
            for (int i = 1; i < add.size(); ++i) p.clips.push_back(add[i]);
            const double shift = oldDur - newTotal;                       // close the gap on the same track
            for (Clip &o : p.clips) if (o.trackId == base.trackId && o.start >= base.start + oldDur - 1e-6 && o.id != base.id) {
                bool ours = false; for (auto &a2 : add) if (a2.id == o.id) ours = true;
                if (!ours) o.start = std::max(0.0, o.start - shift);
            }
        });
        notify(tr("Silence removed: the clip was split into %1 part(s).").arg(segs.size()));
    });
    det->start(mi->path, db, minS, mi->duration);
}

void MainWindow::addTrack(TrackType t) {
    static const QHash<int, QString> names = {{int(TrackType::Video), "Video"}, {int(TrackType::Audio), "Audio"}, {int(TrackType::Music), "Music"}, {int(TrackType::Voice), "Voice-over"}, {int(TrackType::Title), "Titles"}};
    m->mutate(tr("Add track"), [t](Project &p) {
        int count = 1; for (auto &x : p.tracks) if (x.type == t) ++count;
        Track n; n.id = newId("t"); n.type = t; n.name = QString("%1 %2").arg(names[int(t)]).arg(count);
        int idx = p.tracks.size();
        if (t == TrackType::Title) idx = 0;
        else if (t == TrackType::Video) { idx = 0; for (int i = 0; i < p.tracks.size(); ++i) if (p.tracks[i].type == TrackType::Video) { idx = i; break; } }
        p.tracks.insert(idx, n);
    });
}

void MainWindow::showTrackMenu(const QString &id, const QPoint &gp) {
    const Track *t = m->project().trackById(id);
    if (!t) return;
    QMenu menu(this);
    menu.addAction(tr("Rename…"), [this, id] {
        const Track *tt = m->project().trackById(id); if (!tt) return;
        bool ok; const QString n = QInputDialog::getText(this, tr("Rename track"), tr("Name:"), QLineEdit::Normal, tt->name, &ok);
        if (ok && !n.trimmed().isEmpty()) m->mutate(tr("Rename track"), [id, n](Project &p) { if (Track *x = p.trackById(id)) x->name = n.trimmed(); });
    });
    if (t->type != TrackType::Video && t->type != TrackType::Title)
        menu.addAction(tr("Track volume…"), [this, id] {
            const Track *tt = m->project().trackById(id); if (!tt) return;
            bool ok; const int v = QInputDialog::getInt(this, tr("Track volume"), tr("Volume (%):"), int(tt->volume * 100), 0, 400, 5, &ok);
            if (ok) m->mutate(tr("Track volume"), [id, v](Project &p) { if (Track *x = p.trackById(id)) x->volume = v / 100.0; });
        });
    menu.addAction(tr("Taller"), [this, id] { m->mutate(tr("Resize track"), [id](Project &p) { if (Track *x = p.trackById(id)) x->height = std::min(220, x->height + 24); }); });
    menu.addAction(tr("Shorter"), [this, id] { m->mutate(tr("Resize track"), [id](Project &p) { if (Track *x = p.trackById(id)) x->height = std::max(36, x->height - 24); }); });
    menu.addSeparator();
    menu.addAction(tr("Delete track"), [this, id] {
        const int n = m->project().clipsOnTrack(id).size();
        if (QMessageBox::question(this, tr("Delete track"), n ? tr("This track contains %1 clip(s). Delete the track and its clips?").arg(n) : tr("Delete this track?")) != QMessageBox::Yes) return;
        m->mutate(tr("Delete track"), [id](Project &p) {
            p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &c) { return c.trackId == id; }), p.clips.end());
            p.tracks.erase(std::remove_if(p.tracks.begin(), p.tracks.end(), [&](const Track &x) { return x.id == id; }), p.tracks.end());
        });
    });
    menu.exec(gp);
}

void MainWindow::showClipMenu(const QString &id, const QPoint &gp) {
    m->selectClip(id);
    QMenu menu(this);
    for (auto *k : {"split", "dup", "copy", "cut", "delete", "ripple"}) menu.addAction(a[k]);
    menu.addSeparator();
    for (auto *k : {"speed", "reverse", "detach", "mute", "pip"}) menu.addAction(a[k]);
    menu.addMenu(m_transMenu);
    menu.exec(gp);
}

void MainWindow::addMarker(double t) {
    const int n = m->project().markers.size() + 1;
    m->mutate(tr("Add marker"), [t, n](Project &p) { p.markers.push_back({t, QString("M%1").arg(n)}); });
}
void MainWindow::jumpEdge(int dir) {
    QVector<double> pts = {0.0};
    for (auto &c : m->project().clips) { pts << c.start << c.end(); }
    std::sort(pts.begin(), pts.end());
    const double t = m->playhead();
    if (dir > 0) { for (double p : pts) if (p > t + 1e-3) { m->setPlayhead(p); return; } }
    else for (int i = pts.size() - 1; i >= 0; --i) if (pts[i] < t - 1e-3) { m->setPlayhead(pts[i]); return; }
}

// ======================================================================== jobs
bool MainWindow::runJob(const Project &p, const ExportOptions &o, JobKind kind, const QString &label) {
    if (busy()) { QMessageBox::information(this, tr("Busy"), tr("Another render is running. Wait for it to finish or cancel it first.")); return false; }
    m_job = std::make_unique<ExportJob>();
    m_kind = kind; m_jobLabel = label;
    connect(m_job.get(), &ExportJob::progress, this, [this](double f, double eta, const QString &text) {
        m_bar->setVisible(true); m_bar->setValue(int(f * 1000));
        m_bar->setFormat(QString("%1 %p%").arg(m_jobLabel));
        m_stateLabel->setText(text);
        m_etaLabel->setText(eta >= 0 ? tr("%1 remaining").arg(humanTime(eta)) : QString());
        if (m_kind == JobKind::Export) m_exp->setProgress(f, text + (eta >= 0 ? "  –  " + tr("%1 remaining").arg(humanTime(eta)) : QString()));
    });
    connect(m_job.get(), &ExportJob::logLine, this, [this](const QString &l) { m_log->appendPlainText(l.left(600)); });
    connect(m_job.get(), &ExportJob::finished, this, [this](bool ok, const QString &msg, const QString &path) {
        QTimer::singleShot(0, this, [this, ok, msg, path] { onJobFinished(ok, msg, path); });
    });
    QString err;
    if (!m_job->start(p, o, &err)) {
        notify(err, true);
        QMessageBox::warning(this, tr("Cannot start"), err);
        m_job.reset();
        return false;
    }
    m_bar->setVisible(true); m_bar->setValue(0);
    if (kind == JobKind::Export) m_exp->setBusy(true);
    m_stateLabel->setText(tr("Rendering…"));
    return true;
}

void MainWindow::onJobFinished(bool ok, const QString &msg, const QString &path) {
    m_bar->setVisible(false); m_etaLabel->clear(); m_stateLabel->setText(ok ? tr("Ready") : tr("Ready"));
    m_exp->setBusy(false);
    const JobKind kind = m_kind;
    m_job.reset();
    notify(msg, !ok);
    if (kind == JobKind::Preview) {
        if (ok) { m_previewFile = path; m_prev->playRendered(path); }
        else if (!msg.contains("cancel")) QMessageBox::warning(this, tr("Preview"), msg);
        return;
    }
    if (kind == JobKind::Proxy) { if (ok) { m->setProxy(m_jobProxyId, path); notify(tr("Optimized preview is ready.")); } return; }
    if (ok) {
        QMessageBox box(QMessageBox::Information, kind == JobKind::Extract ? tr("Audio extracted") : tr("Export complete"), msg, QMessageBox::NoButton, this);
        auto *play = box.addButton(tr("Play"), QMessageBox::ActionRole);
        auto *folder = box.addButton(tr("Show in folder"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == play) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else if (box.clickedButton() == folder) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    } else if (!msg.contains("cancel")) {
        QMessageBox::critical(this, tr("Export failed"), msg);
    }
}

void MainWindow::startExportWith(const ExportOptions &o) { runJob(m->project(), o, JobKind::Export, tr("Export")); }

void MainWindow::startExport() {
    ExportOptions o = m_exp->currentOptions();
    if (m->project().clips.isEmpty()) { QMessageBox::information(this, tr("Export"), tr("The timeline is empty. Add some clips first.")); return; }
    m_prev->pause();
    if (QFileInfo::exists(o.outputPath) &&
        QMessageBox::question(this, tr("Replace file?"), tr("\"%1\" already exists.\nDo you want to replace it?").arg(QDir::toNativeSeparators(o.outputPath)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    runJob(m->project(), o, JobKind::Export, tr("Export"));
}

void MainWindow::renderPreview() {
    if (m->project().clips.isEmpty()) { notify(tr("The timeline is empty. Add some clips first.")); return; }
    m_prev->releaseMedia();
    if (!m_previewFile.isEmpty()) QFile::remove(m_previewFile);
    const Project &p = m->project();
    ExportOptions o; o.format = "mp4-h264"; o.preview = true; o.quality = "draft"; o.audioBitrateKbps = 96;
    o.height = 480; o.width = int(std::lround(480.0 * p.settings.width / std::max(1, p.settings.height))) & ~1;
    o.fps = std::min(30.0, p.settings.fps > 0 ? p.settings.fps : 30.0);
    QDir().mkpath(AppSettings::dataDir() + "/preview");
    o.outputPath = AppSettings::dataDir() + "/preview/preview_" + QString::number(QDateTime::currentSecsSinceEpoch()) + ".mp4";
    runJob(p, o, JobKind::Preview, tr("Preview"));
}

void MainWindow::extractAudio() {
    const Clip *c = m->project().clipById(m->selectedClip());
    const MediaItem *mi = nullptr;
    if (c && !c->isTitle()) mi = m->project().mediaById(c->mediaId);
    if (!mi) mi = m->project().mediaById(m->selectedMedia());
    if (!mi || !mi->hasAudio) { QMessageBox::information(this, tr("Extract audio"), tr("Select a video or audio clip (or a media library item) that has sound.")); return; }
    bool ok; const QStringList fm = {tr("MP3"), tr("WAV (uncompressed)"), tr("AAC (.m4a)"), tr("FLAC (lossless)")};
    const QString pick = QInputDialog::getItem(this, tr("Extract audio"), tr("Save the sound as:"), fm, 0, false, &ok);
    if (!ok) return;
    const QString id = QStringList{"mp3", "wav", "aac", "flac"}.at(fm.indexOf(pick));
    const FormatInfo fi = formatById(id);
    const QString out = QFileDialog::getSaveFileName(this, tr("Save audio"), QDir(AppSettings::exportDir()).absoluteFilePath(QFileInfo(mi->name).completeBaseName() + "." + fi.ext), fi.label + " (*." + fi.ext + ")");
    if (out.isEmpty()) return;
    Project tmp = c && !c->isTitle() && c->mediaId == mi->id ? Project::singleClip(*mi, c->in, c->out) : Project::singleClip(*mi);
    if (c && c->mediaId == mi->id) tmp.clips[0].fx = c->fx;
    tmp.clips[0].fx.mute = false;
    tmp.tracks[0].type = TrackType::Audio;
    ExportOptions o; o.format = id; o.outputPath = out; o.sampleRate = mi->sampleRate > 0 ? mi->sampleRate : 48000; o.channels = mi->channels == 1 ? 1 : 2;
    o.audioBitrateKbps = 192;
    runJob(tmp, o, JobKind::Extract, tr("Extract"));
}

void MainWindow::createProxy(const QString &mediaId) {
    const MediaItem *mi = m->project().mediaById(mediaId);
    if (!mi || mi->kind != "video") { notify(tr("Select a video in the media library first.")); return; }
    Project tmp = Project::singleClip(*mi);
    ExportOptions o; o.format = "mp4-h264"; o.preview = true; o.quality = "draft"; o.audioBitrateKbps = 96;
    o.height = 540; o.width = int(std::lround(540.0 * mi->width / std::max(1, mi->height))) & ~1;
    o.fps = std::min(30.0, mi->fps > 0 ? mi->fps : 30.0);
    QDir().mkpath(AppSettings::cacheDir() + "/proxies");
    o.outputPath = AppSettings::cacheDir() + "/proxies/" + WaveformCache::keyFor(mi->path) + ".mp4";
    m_jobProxyId = mediaId;
    runJob(tmp, o, JobKind::Proxy, tr("Proxy"));
}

// ======================================================================== help
void MainWindow::showManual() {
    QFile f(":/docs/USER_MANUAL.md"); f.open(QIODevice::ReadOnly);
    TextViewer(tr("M Veditor – User manual"), QString::fromUtf8(f.readAll()), this).exec();
}
void MainWindow::about() {
    QMessageBox::about(this, tr("About M Veditor"),
        tr("<h3>M Veditor %1</h3><p>A standalone video and MP3 editor for Windows.</p>"
           "<p>Powered by Qt %2 and FFmpeg.<br>%3</p>"
           "<p>M Veditor works offline and does not collect or send any data.<br>See Help &gt; Licenses for bundled components.</p>")
            .arg(QStringLiteral(MV_VERSION), QString::fromLatin1(qVersion()), tools::versionString().left(60).toHtmlEscaped()));
}

} // namespace mv
