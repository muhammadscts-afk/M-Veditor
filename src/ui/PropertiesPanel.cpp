#include "PropertiesPanel.h"
#include "MediaProbe.h"
#include <QColorDialog>
#include <QComboBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace mv {

PropertiesPanel::PropertiesPanel(ProjectModel *model, QWidget *parent) : QWidget(parent), m(model) {
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget; scroll->setWidget(content);
    auto *lay = new QVBoxLayout(content);
    m_head = new QLabel(tr("Select a clip on the timeline to edit its properties.")); m_head->setWordWrap(true);
    lay->addWidget(m_head);

    // ---- clip / timing
    gClip = new QGroupBox(tr("Clip")); auto *fc = new QFormLayout(gClip);
    addSpin(fc, tr("Start"), 0, 86400, 0.1, 3, " s", 1, [](const Clip &c) { return c.start; }, [this](Clip &c, double v) { c.start = v; }, tr("Position on the timeline"), false);
    addSpin(fc, tr("Source in"), 0, 86400, 0.1, 3, " s", 1, [](const Clip &c) { return c.in; }, [](Clip &c, double v) { if (v < c.out - 0.05) c.in = v; }, tr("Where the used part of the source starts"), false);
    addSpin(fc, tr("Source out"), 0, 86400, 0.1, 3, " s", 1, [](const Clip &c) { return c.out; }, [this](Clip &c, double v) {
        double mx = 1e9;
        if (const MediaItem *mi = m->project().mediaById(c.mediaId)) if (mi->kind != "image") mx = mi->duration;
        if (v > c.in + 0.05) c.out = std::min(v, mx); }, tr("Where the used part of the source ends"), false);
    m_dur = new QLabel; fc->addRow(tr("Duration"), m_dur);
    m_speed = new QLabel; auto *sb = new QPushButton(tr("Speed…")); sb->setToolTip(tr("Open the speed controller (slow motion / fast motion)"));
    auto *sr = new QHBoxLayout; sr->addWidget(m_speed); sr->addWidget(sb); fc->addRow(tr("Speed"), sr);
    connect(sb, &QPushButton::clicked, this, &PropertiesPanel::speedDialogRequested);
    addSpin(fc, tr("Fade in"), 0, 60, 0.1, 2, " s", 1, [](const Clip &c) { return c.fx.fadeIn; }, [](Clip &c, double v) { c.fx.fadeIn = v; }, tr("Fade picture and sound in at the start"));
    addSpin(fc, tr("Fade out"), 0, 60, 0.1, 2, " s", 1, [](const Clip &c) { return c.fx.fadeOut; }, [](Clip &c, double v) { c.fx.fadeOut = v; }, tr("Fade picture and sound out at the end"));
    lay->addWidget(gClip);
    m_refresh.push_back([this](const Clip &c) {
        m_dur->setText(humanTime(c.duration(), true));
        m_speed->setText(QString::number(c.fx.speed, 'g', 4) + "×" + (c.fx.reverse ? tr(" (reversed)") : QString()));
    });

    // ---- transition
    gTrans = new QGroupBox(tr("Transition in")); auto *ft = new QFormLayout(gTrans);
    auto *tc = new QComboBox;
    const QList<QPair<QString, QString>> trs = {{"none", tr("None")}, {"fade", tr("Fade through black")}, {"dissolve", tr("Cross dissolve")},
        {"wipeleft", tr("Wipe → (left to right)")}, {"wiperight", tr("Wipe ← (right to left)")}, {"wipeup", tr("Wipe ↓ (top to bottom)")}, {"wipedown", tr("Wipe ↑ (bottom to top)")}};
    for (auto &t : trs) tc->addItem(t.second, t.first);
    ft->addRow(tr("Type"), tc);
    connect(tc, &QComboBox::activated, this, [this, tc](int i) { const QString v = tc->itemData(i).toString(); edit(tr("Set transition"), [v](Clip &c) { c.fx.transition = v; }); });
    addSpin(ft, tr("Length"), 0.1, 10, 0.1, 2, " s", 1, [](const Clip &c) { return c.fx.transitionDur; }, [](Clip &c, double v) { c.fx.transitionDur = v; }, tr("For smooth cross dissolves place the clips directly one after another"));
    m_refresh.push_back([tc](const Clip &c) { QSignalBlocker b(tc); tc->setCurrentIndex(std::max(0, tc->findData(c.fx.transition))); });
    lay->addWidget(gTrans);

    // ---- video
    gVideo = new QGroupBox(tr("Picture")); auto *fv = new QFormLayout(gVideo);
    addSpin(fv, tr("Brightness"), -100, 100, 1, 0, " %", 100, [](const Clip &c) { return c.fx.brightness; }, [](Clip &c, double v) { c.fx.brightness = v; });
    addSpin(fv, tr("Contrast"), 0, 300, 1, 0, " %", 100, [](const Clip &c) { return c.fx.contrast; }, [](Clip &c, double v) { c.fx.contrast = v; });
    addSpin(fv, tr("Saturation"), 0, 300, 1, 0, " %", 100, [](const Clip &c) { return c.fx.saturation; }, [](Clip &c, double v) { c.fx.saturation = v; });
    addSpin(fv, tr("Sharpness"), -150, 500, 5, 0, " %", 100, [](const Clip &c) { return c.fx.sharpness; }, [](Clip &c, double v) { c.fx.sharpness = v; }, tr("Positive sharpens, negative softens"));
    addSpin(fv, tr("Balance red"), -100, 100, 1, 0, " %", 100, [](const Clip &c) { return c.fx.balR; }, [](Clip &c, double v) { c.fx.balR = v; });
    addSpin(fv, tr("Balance green"), -100, 100, 1, 0, " %", 100, [](const Clip &c) { return c.fx.balG; }, [](Clip &c, double v) { c.fx.balG = v; });
    addSpin(fv, tr("Balance blue"), -100, 100, 1, 0, " %", 100, [](const Clip &c) { return c.fx.balB; }, [](Clip &c, double v) { c.fx.balB = v; });
    auto *rot = new QComboBox; for (int a : {0, 90, 180, 270}) rot->addItem(QString::number(a) + "°", a);
    fv->addRow(tr("Rotate"), rot);
    connect(rot, &QComboBox::activated, this, [this, rot](int i) { const int v = rot->itemData(i).toInt(); edit(tr("Rotate"), [v](Clip &c) { c.fx.rotation = v; }); });
    m_refresh.push_back([rot](const Clip &c) { QSignalBlocker b(rot); rot->setCurrentIndex(std::max(0, rot->findData(c.fx.rotation))); });
    addCheck(fv, tr("Flip horizontally"), [](const Clip &c) { return c.fx.flipH; }, [](Clip &c, bool v) { c.fx.flipH = v; });
    addCheck(fv, tr("Flip vertically"), [](const Clip &c) { return c.fx.flipV; }, [](Clip &c, bool v) { c.fx.flipV = v; });
    addSpin(fv, tr("Crop left"), 0, 45, 1, 0, " %", 100, [](const Clip &c) { return c.fx.cropL; }, [](Clip &c, double v) { c.fx.cropL = v; });
    addSpin(fv, tr("Crop right"), 0, 45, 1, 0, " %", 100, [](const Clip &c) { return c.fx.cropR; }, [](Clip &c, double v) { c.fx.cropR = v; });
    addSpin(fv, tr("Crop top"), 0, 45, 1, 0, " %", 100, [](const Clip &c) { return c.fx.cropT; }, [](Clip &c, double v) { c.fx.cropT = v; });
    addSpin(fv, tr("Crop bottom"), 0, 45, 1, 0, " %", 100, [](const Clip &c) { return c.fx.cropB; }, [](Clip &c, double v) { c.fx.cropB = v; });
    addSpin(fv, tr("Size"), 5, 300, 5, 0, " %", 100, [](const Clip &c) { return c.fx.scale; }, [](Clip &c, double v) { c.fx.scale = v; }, tr("100% fits the frame. Smaller values give picture-in-picture / overlays"));
    addSpin(fv, tr("Position X"), 0, 100, 5, 0, " %", 100, [](const Clip &c) { return c.fx.posX; }, [](Clip &c, double v) { c.fx.posX = v; });
    addSpin(fv, tr("Position Y"), 0, 100, 5, 0, " %", 100, [](const Clip &c) { return c.fx.posY; }, [](Clip &c, double v) { c.fx.posY = v; });
    addSpin(fv, tr("Opacity"), 0, 100, 5, 0, " %", 100, [](const Clip &c) { return c.fx.opacity; }, [](Clip &c, double v) { c.fx.opacity = v; }, tr("Use together with a lower track for overlays and watermarks"));
    auto *reset = new QPushButton(tr("Reset picture effects"));
    fv->addRow(reset);
    connect(reset, &QPushButton::clicked, this, [this] {
        edit(tr("Reset picture effects"), [](Clip &c) {
            ClipEffects d; auto &f = c.fx;
            f.brightness = d.brightness; f.contrast = d.contrast; f.saturation = d.saturation; f.sharpness = d.sharpness;
            f.balR = f.balG = f.balB = 0; f.rotation = 0; f.flipH = f.flipV = false; f.cropL = f.cropR = f.cropT = f.cropB = 0;
            f.scale = 1; f.posX = f.posY = 0.5; f.opacity = 1; });
    });
    lay->addWidget(gVideo);

    // ---- audio
    gAudio = new QGroupBox(tr("Audio")); auto *fa = new QFormLayout(gAudio);
    addSpin(fa, tr("Volume"), 0, 400, 5, 0, " %", 100, [](const Clip &c) { return c.fx.volume; }, [](Clip &c, double v) { c.fx.volume = v; }, tr("Clip gain. 100% is unchanged"));
    addCheck(fa, tr("Mute clip"), [](const Clip &c) { return c.fx.mute; }, [](Clip &c, bool v) { c.fx.mute = v; });
    addCheck(fa, tr("Normalize loudness"), [](const Clip &c) { return c.fx.normalize; }, [](Clip &c, bool v) { c.fx.normalize = v; }, tr("Brings the clip to a standard loudness (-16 LUFS)"));
    addCheck(fa, tr("Reduce noise"), [](const Clip &c) { return c.fx.denoise; }, [](Clip &c, bool v) { c.fx.denoise = v; }, tr("Basic broadband noise reduction"));
    addCheck(fa, tr("Compressor"), [](const Clip &c) { return c.fx.compressor; }, [](Clip &c, bool v) { c.fx.compressor = v; }, tr("Evens out loud and quiet parts"));
    addSpin(fa, tr("Bass"), -15, 15, 1, 0, " dB", 1, [](const Clip &c) { return c.fx.eqBass; }, [](Clip &c, double v) { c.fx.eqBass = v; });
    addSpin(fa, tr("Mid"), -15, 15, 1, 0, " dB", 1, [](const Clip &c) { return c.fx.eqMid; }, [](Clip &c, double v) { c.fx.eqMid = v; });
    addSpin(fa, tr("Treble"), -15, 15, 1, 0, " dB", 1, [](const Clip &c) { return c.fx.eqTreble; }, [](Clip &c, double v) { c.fx.eqTreble = v; });
    addSpin(fa, tr("Pitch"), -12, 12, 1, 0, " semitones", 1, [](const Clip &c) { return c.fx.pitchSemitones; }, [](Clip &c, double v) { c.fx.pitchSemitones = v; }, tr("Changes pitch without changing speed"));
    addCheck(fa, tr("Keep pitch when speed changes"), [](const Clip &c) { return c.fx.preservePitch; }, [](Clip &c, bool v) { c.fx.preservePitch = v; });
    lay->addWidget(gAudio);

    // ---- title
    gTitle = new QGroupBox(tr("Title / caption")); auto *ftt = new QFormLayout(gTitle);
    m_text = new QPlainTextEdit; m_text->setFixedHeight(64); ftt->addRow(tr("Text"), m_text);
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        if (m_busy) return; const QString t = m_text->toPlainText();
        const QString id = m->selectedClip();
        m->mutate(tr("Edit title text"), [id, t](Project &p) { if (Clip *c = p.clipById(id)) c->title.text = t; }, "title-text:" + id);
    });
    addSpin(ftt, tr("Size"), 8, 400, 4, 0, " px", 1, [](const Clip &c) { return c.title.fontSize; }, [](Clip &c, double v) { c.title.fontSize = v; }, tr("Height in pixels at 1080p; scales with the export size"));
    m_color = new QPushButton(tr("Text colour…")); m_boxColor = new QPushButton(tr("Box colour…"));
    ftt->addRow(m_color); ftt->addRow(m_boxColor);
    connect(m_color, &QPushButton::clicked, this, [this] {
        const Clip *c = m->project().clipById(m->selectedClip()); if (!c) return;
        QColor col = QColorDialog::getColor(QColor(c->title.color), this, tr("Text colour")); if (!col.isValid()) return;
        edit(tr("Title colour"), [col](Clip &x) { x.title.color = col.name(); });
    });
    connect(m_boxColor, &QPushButton::clicked, this, [this] {
        const Clip *c = m->project().clipById(m->selectedClip()); if (!c) return;
        QColor col = QColorDialog::getColor(QColor(c->title.boxColor), this, tr("Box colour")); if (!col.isValid()) return;
        edit(tr("Title box colour"), [col](Clip &x) { x.title.boxColor = col.name(); });
    });
    addCheck(ftt, tr("Background box"), [](const Clip &c) { return c.title.box; }, [](Clip &c, bool v) { c.title.box = v; });
    addSpin(ftt, tr("Box opacity"), 0, 100, 5, 0, " %", 100, [](const Clip &c) { return c.title.boxOpacity; }, [](Clip &c, double v) { c.title.boxOpacity = v; });
    addSpin(ftt, tr("Position X"), 0, 100, 5, 0, " %", 100, [](const Clip &c) { return c.title.posX; }, [](Clip &c, double v) { c.title.posX = v; });
    addSpin(ftt, tr("Position Y"), 0, 100, 5, 0, " %", 100, [](const Clip &c) { return c.title.posY; }, [](Clip &c, double v) { c.title.posY = v; }, tr("Bottom is 100%. Subtitles usually sit around 85%"));
    m_refresh.push_back([this](const Clip &c) { QSignalBlocker b(m_text); if (m_text->toPlainText() != c.title.text) m_text->setPlainText(c.title.text); });
    lay->addWidget(gTitle);
    lay->addStretch(1);

    connect(m, &ProjectModel::selectionChanged, this, &PropertiesPanel::refresh);
    connect(m, &ProjectModel::changed, this, &PropertiesPanel::refresh);
    refresh();
}

void PropertiesPanel::edit(const QString &what, const std::function<void(Clip &)> &fn) {
    if (m_busy) return;
    const QStringList ids = m->selectedClips();           // applies to every selected clip
    if (ids.isEmpty()) return;
    m->mutate(what, [ids, fn](Project &p) { for (const QString &id : ids) if (Clip *c = p.clipById(id)) fn(*c); });
}

QDoubleSpinBox *PropertiesPanel::addSpin(QFormLayout *f, const QString &label, double mn, double mx, double step, int dec, const QString &suffix,
                                         double scale, Getter g, Setter s, const QString &tip, bool multi) {
    auto *sp = new QDoubleSpinBox; sp->setRange(mn, mx); sp->setSingleStep(step); sp->setDecimals(dec); sp->setSuffix(suffix);
    sp->setKeyboardTracking(false); sp->setAccessibleName(label);
    if (!tip.isEmpty()) sp->setToolTip(tip);
    f->addRow(label, sp);
    connect(sp, &QDoubleSpinBox::valueChanged, this, [this, s, scale, label, multi](double v) {
        if (m_busy) return;
        QStringList ids = multi ? m->selectedClips() : QStringList{m->selectedClip()};
        ids.removeAll(QString());
        if (ids.isEmpty()) return;
        m->mutate(tr("Change %1").arg(label), [ids, s, v, scale](Project &p) { for (const QString &id : ids) if (Clip *c = p.clipById(id)) s(*c, v / scale); }, "prop:" + ids.join(',') + ":" + label);
    });
    m_refresh.push_back([sp, g, scale](const Clip &c) { QSignalBlocker b(sp); sp->setValue(g(c) * scale); });
    return sp;
}
QCheckBox *PropertiesPanel::addCheck(QFormLayout *f, const QString &label, std::function<bool(const Clip &)> g, std::function<void(Clip &, bool)> s, const QString &tip) {
    auto *cb = new QCheckBox(label); if (!tip.isEmpty()) cb->setToolTip(tip);
    f->addRow(cb);
    connect(cb, &QCheckBox::toggled, this, [this, s, label](bool v) { edit(tr("Change %1").arg(label), [s, v](Clip &c) { s(c, v); }); });
    m_refresh.push_back([cb, g](const Clip &c) { QSignalBlocker b(cb); cb->setChecked(g(c)); });
    return cb;
}

void PropertiesPanel::refresh() {
    const Clip *c = m->project().clipById(m->selectedClip());
    const bool has = c != nullptr;
    for (auto *g : {gClip, gTrans, gVideo, gAudio, gTitle}) g->setVisible(has);
    m_head->setVisible(!has);
    if (!has) return;
    const MediaItem *mi = c->isTitle() ? nullptr : m->project().mediaById(c->mediaId);
    const Track *tr = m->project().trackById(c->trackId);
    const bool isTitle = c->isTitle();
    const bool audioOnlyTrack = tr && isAudioType(tr->type);
    const bool hasVideo = !isTitle && mi && mi->hasVideo && !audioOnlyTrack;
    const bool hasAudio = !isTitle && mi && mi->hasAudio;
    gTitle->setVisible(isTitle);
    gVideo->setVisible(hasVideo);
    gAudio->setVisible(hasAudio);
    gTrans->setVisible(hasVideo || isTitle ? !isTitle : false);
    gClip->setTitle(isTitle ? QObject::tr("Title clip") : (mi ? mi->name : QObject::tr("Clip")));
    m_busy = true;
    for (auto &r : m_refresh) r(*c);
    m_busy = false;
}

} // namespace mv
