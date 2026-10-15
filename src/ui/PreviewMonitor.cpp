#include "PreviewMonitor.h"
#include "AppSettings.h"
#include "MediaProbe.h"
#include <QFileInfo>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

namespace mv {

PreviewMonitor::PreviewMonitor(ProjectModel *model, QWidget *parent) : QWidget(parent), m(model) {
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setBackgroundBrush(QColor(10, 10, 12));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform);
    m_view->setMinimumSize(320, 180);
    m_view->setAccessibleName(tr("Preview monitor"));
    m_canvas = m_scene->addRect(0, 0, 1920, 1080, QPen(Qt::NoPen), QBrush(Qt::black));
    m_video = new QGraphicsVideoItem; m_video->setAspectRatioMode(Qt::KeepAspectRatio); m_scene->addItem(m_video);
    m_image = m_scene->addPixmap(QPixmap()); m_image->setVisible(false);
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_video);

    m_slider = new QSlider(Qt::Horizontal); m_slider->setRange(0, 10000);
    m_slider->setToolTip(tr("Seek"));
    m_time = new QLabel("00:00.000 / 00:00.000"); m_time->setMinimumWidth(170);
    m_modeLabel = new QLabel; m_modeLabel->setStyleSheet("color:#8fb4ff; font-weight:bold;");
    auto mk = [&](QStyle::StandardPixmap ic, const QString &tip) { auto *b = new QToolButton; b->setIcon(style()->standardIcon(ic)); b->setToolTip(tip); b->setAutoRaise(true); b->setIconSize(QSize(22, 22)); return b; };
    m_play = mk(QStyle::SP_MediaPlay, tr("Play / Pause (Space)"));
    auto *stopB = mk(QStyle::SP_MediaStop, tr("Stop and return to start"));
    auto *prev = mk(QStyle::SP_MediaSeekBackward, tr("Previous frame (Left)"));
    auto *next = mk(QStyle::SP_MediaSeekForward, tr("Next frame (Right)"));
    m_srcBtn = new QToolButton; m_srcBtn->setText(tr("Source")); m_srcBtn->setCheckable(true); m_srcBtn->setToolTip(tr("Preview the selected library item"));
    m_tlBtn = new QToolButton; m_tlBtn->setText(tr("Timeline")); m_tlBtn->setCheckable(true); m_tlBtn->setChecked(true); m_tlBtn->setToolTip(tr("Preview the timeline at the playhead"));
    auto *rend = new QPushButton(tr("Render preview")); rend->setToolTip(tr("Render the whole timeline (all tracks, effects, transitions, titles and mix) at low resolution and play it (F5)"));
    auto *markIn = new QToolButton; markIn->setText(tr("Mark In")); markIn->setToolTip(tr("Set the start of the part of the source you want to use"));
    auto *markOut = new QToolButton; markOut->setText(tr("Mark Out")); markOut->setToolTip(tr("Set the end of the part of the source you want to use"));
    auto *addB = new QToolButton; addB->setText(tr("Add to timeline")); addB->setToolTip(tr("Add the source (or the marked part) at the playhead"));

    auto *lay = new QVBoxLayout(this); lay->setContentsMargins(0, 0, 0, 0); lay->setSpacing(2);
    lay->addWidget(m_view, 1);
    lay->addWidget(m_slider);
    auto *row = new QHBoxLayout; row->setContentsMargins(6, 0, 6, 4);
    row->addWidget(prev); row->addWidget(m_play); row->addWidget(stopB); row->addWidget(next); row->addWidget(m_time);
    row->addSpacing(8); row->addWidget(m_srcBtn); row->addWidget(m_tlBtn); row->addWidget(m_modeLabel);
    row->addStretch(1);
    row->addWidget(markIn); row->addWidget(markOut); row->addWidget(addB); row->addWidget(rend);
    lay->addLayout(row);

    connect(m_play, &QToolButton::clicked, this, &PreviewMonitor::togglePlay);
    connect(stopB, &QToolButton::clicked, this, &PreviewMonitor::stop);
    connect(prev, &QToolButton::clicked, this, [this] { stepFrame(-1); });
    connect(next, &QToolButton::clicked, this, [this] { stepFrame(1); });
    connect(rend, &QPushButton::clicked, this, &PreviewMonitor::renderPreviewRequested);
    connect(m_srcBtn, &QToolButton::clicked, this, [this] {
        const QString id = m->selectedMedia().isEmpty() ? m_sourceId : m->selectedMedia();
        if (!id.isEmpty()) playSource(id); else { m_srcBtn->setChecked(false); emit message(tr("Select an item in the media library first.")); }
    });
    connect(m_tlBtn, &QToolButton::clicked, this, [this] { pause(); setMode(Mode::Timeline); syncTo(m->playhead(), false); });
    connect(markIn, &QToolButton::clicked, this, [this] { if (m_mode == Mode::Source) { m_markIn = m_player->position() / 1000.0; emit message(tr("In point set to %1").arg(humanTime(m_markIn, true))); } });
    connect(markOut, &QToolButton::clicked, this, [this] { if (m_mode == Mode::Source) { m_markOut = m_player->position() / 1000.0; emit message(tr("Out point set to %1").arg(humanTime(m_markOut, true))); } });
    connect(addB, &QToolButton::clicked, this, [this] {
        if (m_mode != Mode::Source || m_sourceId.isEmpty()) { emit message(tr("Open a library item in Source mode first.")); return; }
        double a = std::max(0.0, m_markIn), b = m_markOut;
        const MediaItem *mi = m->project().mediaById(m_sourceId);
        if (!mi) return;
        if (b <= a) b = mi->duration;
        emit addToTimelineRequested(m_sourceId, a, b);
    });
    connect(m_slider, &QSlider::sliderPressed, this, [this] { m_sliderDrag = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this] { m_sliderDrag = false; });
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_sliderDrag) return;
        if (m_mode == Mode::Timeline) { m->setPlayhead(v / 10000.0 * std::max(1.0, m->project().duration())); }
        else if (m_player->duration() > 0) m_player->setPosition(qint64(v / 10000.0 * m_player->duration()));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (m_mode == Mode::Timeline) return;
        if (m_mode == Mode::Rendered) m->setPlayhead(pos / 1000.0, true);
        if (!m_sliderDrag && m_player->duration() > 0) { QSignalBlocker b(m_slider); m_slider->setValue(int(10000.0 * pos / m_player->duration())); }
        updateTimeLabel();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        if (m_mode != Mode::Timeline) m_play->setIcon(style()->standardIcon(s == QMediaPlayer::PlayingState ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
        if ((st == QMediaPlayer::LoadedMedia || st == QMediaPlayer::BufferedMedia || st == QMediaPlayer::BufferingMedia) && m_pending.valid) {
            m_pending.valid = false;
            const Clip *c = activeVideoClip(m->playhead());
            applyToPlayer(m_pending.posMs, m_pending.play, c ? c->fx.speed : 1.0, c ? c->fx.volume : 1.0);
        }
        if (st == QMediaPlayer::InvalidMedia) emit message(tr("This file can't be previewed (unsupported or damaged). Exporting may still work."));
    });
    connect(m_video, &QGraphicsVideoItem::nativeSizeChanged, this, [this] { fit(); });
    connect(&m_timer, &QTimer::timeout, this, &PreviewMonitor::tick);
    m_timer.setInterval(33);
    connect(m, &ProjectModel::playheadChanged, this, &PreviewMonitor::onPlayheadChanged);
    connect(m, &ProjectModel::changed, this, [this] {
        if (m_mode == Mode::Timeline && !m_tlPlaying) syncTo(m->playhead(), false);
        updateTimeLabel();
    });
    setMode(Mode::Timeline);
    setCanvas(m->project().settings.width, m->project().settings.height);
}

void PreviewMonitor::showEvent(QShowEvent *) { fit(); }
void PreviewMonitor::resizeEvent(QResizeEvent *) { fit(); }
void PreviewMonitor::setCanvas(int w, int h) {
    m_cw = std::max(16, w); m_ch = std::max(16, h);
    m_canvas->setRect(0, 0, m_cw, m_ch);
    m_video->setSize(QSizeF(m_cw, m_ch));
    m_scene->setSceneRect(0, 0, m_cw, m_ch);
    fit();
}
void PreviewMonitor::fit() { m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio); }

void PreviewMonitor::setMode(Mode md) {
    m_mode = md;
    m_srcBtn->setChecked(md == Mode::Source || md == Mode::Rendered);
    m_tlBtn->setChecked(md == Mode::Timeline);
    m_modeLabel->setText(md == Mode::Source ? tr("SOURCE") : md == Mode::Rendered ? tr("RENDERED PREVIEW") : tr("TIMELINE (live)"));
    if (md != Mode::Timeline) {
        for (auto *i : m_titleItems) { m_scene->removeItem(i); delete i; }
        m_titleItems.clear();
    }
}

void PreviewMonitor::releaseMedia() {
    pause(); m_player->setSource(QUrl()); m_loadedPath.clear(); m_pending.valid = false; m_sourceId.clear();
}

void PreviewMonitor::playSource(const QString &mediaId) {
    const MediaItem *mi = m->project().mediaById(mediaId);
    if (!mi) return;
    pause();
    if (mi->missing) { emit message(tr("This file is missing. Use File > Relink Missing Media.")); return; }
    setMode(Mode::Source);
    m_sourceId = mediaId; m_markIn = m_markOut = -1; m_srcFps = mi->fps > 0 ? mi->fps : 30;
    setCanvas(mi->width ? mi->width : 1280, mi->height ? mi->height : 720);
    m_image->setVisible(false);
    if (mi->kind == "image") {
        QPixmap pm(mi->path); m_image->setPixmap(pm);
        m_image->setScale(std::min(double(m_cw) / pm.width(), double(m_ch) / pm.height()));
        m_image->setVisible(true); m_video->setVisible(false);
        m_player->setSource(QUrl()); m_loadedPath.clear();
        return;
    }
    m_video->setVisible(mi->hasVideo);
    const QString path = m->playablePath(mediaId, AppSettings::useProxies());
    m_loadedPath = path;
    m_player->setPlaybackRate(1.0); m_audio->setVolume(1.0);
    m_player->setSource(QUrl::fromLocalFile(path));
    m_pending = {true, 0, true};
}

void PreviewMonitor::playRendered(const QString &path) {
    pause();
    setMode(Mode::Rendered);
    setCanvas(m->project().settings.width, m->project().settings.height);
    m_image->setVisible(false); m_video->setVisible(true);
    m_renderedPath = path; m_loadedPath = path;
    m_player->setPlaybackRate(1.0); m_audio->setVolume(1.0);
    m_player->setSource(QUrl::fromLocalFile(path));
    m_pending = {true, qint64(m->playhead() * 1000), true};
}

bool PreviewMonitor::isPlaying() const { return m_tlPlaying || m_player->playbackState() == QMediaPlayer::PlayingState; }

void PreviewMonitor::togglePlay() {
    if (isPlaying()) { pause(); return; }
    if (m_mode == Mode::Timeline) {
        const double dur = m->project().duration();
        if (dur <= 0) { emit message(tr("The timeline is empty. Add clips first.")); return; }
        if (m->playhead() >= dur - 0.05) m->setPlayhead(0);
        m_t0 = m->playhead(); m_clock.start(); m_tlPlaying = true; m_timer.start();
        m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        syncTo(m_t0, true);
    } else {
        if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) m_player->setPosition(0);
        m_player->play();
    }
}
void PreviewMonitor::pause() {
    if (m_tlPlaying) { m_tlPlaying = false; m_timer.stop(); }
    m_player->pause();
    m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
}
void PreviewMonitor::stop() {
    pause();
    if (m_mode == Mode::Timeline) m->setPlayhead(0);
    else m_player->setPosition(0);
}
void PreviewMonitor::stepFrame(int dir) {
    pause();
    if (m_mode == Mode::Timeline) {
        m->setPlayhead(std::max(0.0, m->playhead() + dir / std::max(1.0, m->project().settings.fps)));
    } else {
        m_player->setPosition(std::max<qint64>(0, m_player->position() + qint64(dir * 1000.0 / m_srcFps)));
    }
}

const Clip *PreviewMonitor::activeVideoClip(double t) const {
    const Project &p = m->project();
    for (const Track &tr : p.tracks) {
        if (tr.type != TrackType::Video || tr.hidden) continue;
        for (const Clip &c : p.clips) {
            if (c.trackId != tr.id || c.isTitle()) continue;
            const MediaItem *mi = p.mediaById(c.mediaId);
            if (mi && mi->hasVideo && t >= c.start - 1e-6 && t < c.end() - 1e-6) return &c;
        }
    }
    return nullptr;
}

void PreviewMonitor::applyToPlayer(qint64 posMs, bool play, double rate, double vol) {
    if (std::llabs(m_player->position() - posMs) > (play ? 350 : 20)) m_player->setPosition(posMs);
    m_player->setPlaybackRate(std::clamp(rate, 0.1, 16.0));
    m_audio->setVolume(float(std::clamp(vol, 0.0, 1.0)));
    if (play) m_player->play(); else m_player->pause();
}

void PreviewMonitor::syncTo(double t, bool play) {
    const Clip *c = activeVideoClip(t);
    updateTitles(t);
    updateTimeLabel();
    if (!c) { m_player->pause(); m_video->setVisible(false); m_image->setVisible(false); return; }
    const MediaItem *mi = m->project().mediaById(c->mediaId);
    if (!mi || mi->missing) { m_player->pause(); m_video->setVisible(false); return; }
    if (mi->kind == "image") {
        m_player->pause(); m_video->setVisible(false);
        if (m_activeImageId != mi->id) {
            m_activeImageId = mi->id;
            QPixmap pm(mi->path); m_image->setPixmap(pm);
            m_image->setScale(std::min(double(m_cw) / std::max(1, pm.width()), double(m_ch) / std::max(1, pm.height())));
        }
        m_image->setVisible(true);
        return;
    }
    m_image->setVisible(false); m_video->setVisible(true);
    const Track *tr = m->project().trackById(c->trackId);
    const QString path = m->playablePath(mi->id, AppSettings::useProxies());
    const qint64 target = qint64((c->in + (t - c->start) * c->fx.speed) * 1000.0);
    const double vol = c->fx.mute ? 0.0 : c->fx.volume * (tr ? tr->volume : 1.0);
    if (m_loadedPath != path) {
        m_loadedPath = path;
        m_pending = {true, target, play};
        m_player->setSource(QUrl::fromLocalFile(path));
        m_audio->setVolume(float(std::clamp(vol, 0.0, 1.0)));
        m_player->setPlaybackRate(std::clamp(c->fx.speed, 0.1, 16.0));
    } else if (m_pending.valid) {
        m_pending = {true, target, play};
    } else {
        applyToPlayer(target, play, c->fx.speed, vol);
    }
}

void PreviewMonitor::updateTitles(double t) {
    for (auto *i : m_titleItems) { m_scene->removeItem(i); delete i; }
    m_titleItems.clear();
    if (m_mode != Mode::Timeline) return;
    const Project &p = m->project();
    for (const Track &tr : p.tracks) {
        if (tr.type != TrackType::Title || tr.hidden) continue;
        for (const Clip &c : p.clips) {
            if (c.trackId != tr.id || t < c.start || t >= c.end()) continue;
            auto *txt = new QGraphicsSimpleTextItem(c.title.text);
            QFont f; f.setBold(true); f.setPixelSize(std::max(8, int(c.title.fontSize * m_ch / 1080.0)));
            txt->setFont(f); txt->setBrush(QColor(c.title.color));
            const QRectF br = txt->boundingRect();
            const double pad = c.title.box ? f.pixelSize() * 0.3 : 0;
            const double x = (m_cw - br.width() - 2 * pad) * c.title.posX + pad, y = (m_ch - br.height() - 2 * pad) * c.title.posY + pad;
            if (c.title.box) {
                QColor bc(c.title.boxColor); bc.setAlphaF(c.title.boxOpacity);
                auto *box = m_scene->addRect(QRectF(x - pad, y - pad, br.width() + 2 * pad, br.height() + 2 * pad), Qt::NoPen, bc);
                box->setZValue(10); m_titleItems.push_back(box);
            }
            txt->setPos(x, y); txt->setZValue(11);
            m_scene->addItem(txt); m_titleItems.push_back(txt);
        }
    }
}

void PreviewMonitor::tick() {
    const double t = m_t0 + m_clock.elapsed() / 1000.0;
    const double dur = m->project().duration();
    if (t >= dur) { m->setPlayhead(dur, true); pause(); return; }
    m->setPlayhead(t, true);
    syncTo(t, true);
}

void PreviewMonitor::onPlayheadChanged(double t, bool fromPlayback) {
    if (fromPlayback) { updateTimeLabel(); return; }
    if (m_mode == Mode::Rendered) { m_player->setPosition(qint64(t * 1000)); return; }
    if (m_mode == Mode::Source) { pause(); setMode(Mode::Timeline); setCanvas(m->project().settings.width, m->project().settings.height); m_loadedPath.clear(); }
    if (m_tlPlaying) { m_t0 = t; m_clock.restart(); }
    syncTo(t, m_tlPlaying);
}

void PreviewMonitor::updateTimeLabel() {
    if (m_mode == Mode::Timeline) {
        const double dur = m->project().duration();
        m_time->setText(humanTime(m->playhead(), true) + " / " + humanTime(dur, true));
        if (!m_sliderDrag) { QSignalBlocker b(m_slider); m_slider->setValue(dur > 0 ? int(10000.0 * m->playhead() / dur) : 0); }
    } else {
        m_time->setText(humanTime(m_player->position() / 1000.0, true) + " / " + humanTime(m_player->duration() / 1000.0, true));
    }
}

} // namespace mv
