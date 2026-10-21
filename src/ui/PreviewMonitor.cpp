#include "PreviewMonitor.h"
#include "AppSettings.h"
#include "Theme.h"
#include "MediaProbe.h"
#include <QFileInfo>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QJsonDocument>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace mv {

// ======================================================================= icons / volume box / full screen window
QIcon uiIcon(const QString &kind, const QColor &c) {
    QPixmap pm(48, 48); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(c);
    if (kind == "play") { QPolygonF t; t << QPointF(14, 8) << QPointF(14, 40) << QPointF(40, 24); p.drawPolygon(t); }
    else if (kind == "pause") { p.drawRoundedRect(QRectF(12, 8, 9, 32), 2, 2); p.drawRoundedRect(QRectF(27, 8, 9, 32), 2, 2); }
    else if (kind == "speaker" || kind == "muted") {
        QPolygonF sp; sp << QPointF(6, 18) << QPointF(15, 18) << QPointF(26, 8) << QPointF(26, 40) << QPointF(15, 30) << QPointF(6, 30); p.drawPolygon(sp);
        p.setBrush(Qt::NoBrush); p.setPen(QPen(c, 3.4, Qt::SolidLine, Qt::RoundCap));
        if (kind == "speaker") { p.drawArc(QRectF(21, 15, 14, 18), -60 * 16, 120 * 16); p.drawArc(QRectF(21, 8, 24, 32), -60 * 16, 120 * 16); }
        else { p.drawLine(QPointF(32, 17), QPointF(44, 31)); p.drawLine(QPointF(44, 17), QPointF(32, 31)); }
    }
    return QIcon(pm);
}

VolumeBox::VolumeBox(const QColor &col, QWidget *parent) : QWidget(parent), m_color(col) {
    auto *l = new QHBoxLayout(this); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
    m_btn = new QToolButton; m_btn->setAutoRaise(true); m_btn->setIconSize(QSize(18, 18)); m_btn->setFocusPolicy(Qt::NoFocus);
    m_btn->setToolTip(tr("Volume – click to mute / unmute"));
    m_slider = new QSlider(Qt::Horizontal); m_slider->setRange(0, 100); m_slider->setFixedWidth(64); m_slider->setFocusPolicy(Qt::NoFocus);
    m_slider->setToolTip(tr("Preview volume"));
    m_slider->setStyleSheet("QSlider::groove:horizontal { height: 4px; background: #5a5e68; border-radius: 2px; } QSlider::sub-page:horizontal { background: #d0d3da; border-radius: 2px; }"
                            "QSlider::handle:horizontal { width: 10px; margin: -4px 0; border-radius: 5px; background: #ffffff; }");
    l->addWidget(m_btn); l->addWidget(m_slider);
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_pct = v;
        if (v > 0 && m_muted) { m_muted = false; emit muteToggled(false); }
        refresh(); emit volumeChanged(v);
    });
    connect(m_btn, &QToolButton::clicked, this, [this] { m_muted = !m_muted; refresh(); emit muteToggled(m_muted); });
    refresh();
}
void VolumeBox::setVolume(int pct) { QSignalBlocker b(m_slider); m_slider->setValue(pct); m_pct = pct; refresh(); }
void VolumeBox::setMuted(bool m) { m_muted = m; refresh(); }
void VolumeBox::refresh() { m_btn->setIcon(uiIcon(m_muted || m_pct == 0 ? "muted" : "speaker", m_color)); }

FullscreenWindow::FullscreenWindow(QGraphicsView *view) : QWidget(nullptr, Qt::Window), m_view(view) {
    setObjectName("mvfs");
    setWindowTitle(tr("M Veditor – full screen"));
    setStyleSheet("#mvfs { background: black; } #fsbar { background: rgba(15,15,18,215); } #fsbar QLabel { color: white; font-size: 10pt; }"
                  "#fsbar QToolButton { color: white; border: none; padding: 4px 8px; font-size: 10pt; } #fsbar QToolButton:hover { background: rgba(255,255,255,45); }"
                  "QToolButton#abBtn { border: 1px solid #aaa; border-radius: 3px; font-weight: bold; }"
                  "QToolButton[abState=\"1\"] { background: #b8860b; } QToolButton[abState=\"2\"] { background: #2d9d4a; }");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_view->setParent(this);                                                  // the picture's own view moves in here: no second, mis-scaled copy
    m_view->setBackgroundBrush(Qt::black);
    m_view->setFocusPolicy(Qt::NoFocus); m_view->viewport()->setFocusPolicy(Qt::NoFocus); m_view->viewport()->setMouseTracking(true);
    m_bar = new QWidget(this); m_bar->setObjectName("fsbar"); m_bar->setMouseTracking(true); m_bar->installEventFilter(this);
    const QColor white(245, 245, 248);
    play = new QToolButton; play->setIcon(uiIcon("play", white)); play->setIconSize(QSize(26, 26)); play->setFocusPolicy(Qt::NoFocus);
    vol = new VolumeBox(white);
    time = new QLabel("00:00.000 / 00:00.000"); time->setMinimumWidth(190);
    slider = new SeekSlider(Qt::Horizontal); slider->setRange(0, 10000); slider->setFocusPolicy(Qt::NoFocus);
    slider->setStyleSheet("QSlider::groove:horizontal { height: 8px; background: #50545e; border-radius: 4px; } QSlider::sub-page:horizontal { background: #2d7ff9; border-radius: 4px; }"
                          "QSlider::handle:horizontal { width: 18px; margin: -6px 0; border-radius: 9px; background: #ff4757; border: 2px solid #ffffff; }");
    abBtn = new QToolButton; abBtn->setObjectName("abBtn"); abBtn->setText("A B"); abBtn->setFocusPolicy(Qt::NoFocus);
    auto *exitB = new QToolButton; exitB->setText(tr("Exit full screen (Esc)")); exitB->setFocusPolicy(Qt::NoFocus);
    auto *lay = new QHBoxLayout(m_bar); lay->setContentsMargins(14, 8, 14, 8); lay->setSpacing(10);
    lay->addWidget(play); lay->addWidget(vol); lay->addWidget(time); lay->addWidget(slider, 1); lay->addWidget(abBtn); lay->addWidget(exitB);
    connect(play, &QToolButton::clicked, this, &FullscreenWindow::togglePlayRequested);
    connect(exitB, &QToolButton::clicked, this, &FullscreenWindow::exitRequested);
    m_hide.setSingleShot(true); m_hide.setInterval(2500);
    connect(&m_hide, &QTimer::timeout, this, &FullscreenWindow::hideBar);
}
void FullscreenWindow::showBar() {
    m_bar->show(); m_bar->raise();
    m_view->viewport()->unsetCursor(); unsetCursor();
    m_hide.start();
}
void FullscreenWindow::hideBar() {
    if (m_bar->underMouse()) { m_hide.start(); return; }                    // never hide while the pointer is on the bar
    m_bar->hide(); m_view->viewport()->setCursor(Qt::BlankCursor); setCursor(Qt::BlankCursor);
}
void FullscreenWindow::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) emit exitRequested();
    else if (e->key() == Qt::Key_Space) emit togglePlayRequested();
    else QWidget::keyPressEvent(e);
}
void FullscreenWindow::closeEvent(QCloseEvent *e) { e->ignore(); emit exitRequested(); }
void FullscreenWindow::showEvent(QShowEvent *) { m_view->setGeometry(rect()); m_view->show(); m_bar->raise(); }
void FullscreenWindow::resizeEvent(QResizeEvent *) {
    m_view->setGeometry(rect());
    m_bar->setGeometry(0, height() - 60, width(), 60); m_bar->raise();
}
bool FullscreenWindow::eventFilter(QObject *o, QEvent *e) {
    if (o == m_bar && (e->type() == QEvent::MouseMove || e->type() == QEvent::Enter)) showBar();
    return QWidget::eventFilter(o, e);
}

static constexpr double kLookahead = 2.5;   // seconds: how early the next clip / audio clip is prepared

PreviewMonitor::PreviewMonitor(ProjectModel *model, QWidget *parent) : QWidget(parent), m(model) {
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setBackgroundBrush(QColor(10, 10, 12));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform);
    m_view->setMinimumSize(240, 135);
    m_view->setAccessibleName(tr("Preview monitor"));
    m_canvas = m_scene->addRect(0, 0, 1920, 1080, QPen(Qt::NoPen), QBrush(Qt::black));
    m_video = new QGraphicsVideoItem; m_video->setAspectRatioMode(Qt::KeepAspectRatio); m_scene->addItem(m_video);
    m_pre.vi = new QGraphicsVideoItem; m_pre.vi->setAspectRatioMode(Qt::KeepAspectRatio); m_scene->addItem(m_pre.vi); m_pre.vi->setVisible(false);
    m_image = m_scene->addPixmap(QPixmap()); m_image->setVisible(false);
    m_busy = m_scene->addSimpleText(QString()); m_busy->setBrush(Qt::white); m_busy->setZValue(30); m_busy->setVisible(false);
    m_note = m_scene->addSimpleText(QString()); m_note->setBrush(QColor(255, 255, 255, 170)); m_note->setZValue(29); m_note->setVisible(false);
    m_badge = m_scene->addSimpleText(QString()); m_badge->setBrush(QColor(255, 220, 80)); m_badge->setZValue(28); m_badge->setVisible(false);
    m_player = new QMediaPlayer(this); m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio); m_player->setVideoOutput(m_video);
    m_pitch = new PitchAudio(this);
    m_pre.pl = new QMediaPlayer(this); m_pre.ao = new QAudioOutput(this);
    m_pre.pl->setAudioOutput(m_pre.ao); m_pre.pl->setVideoOutput(m_pre.vi);

    m_slider = new SeekSlider(Qt::Horizontal); m_slider->setRange(0, 10000);
    m_slider->setToolTip(tr("Click or drag to jump to any point"));
    m_slider->setFixedHeight(20);
    m_slider->setStyleSheet(
        "QSlider::groove:horizontal { height: 8px; background: #3a3d45; border-radius: 4px; }"
        "QSlider::sub-page:horizontal { background: #2d7ff9; border-radius: 4px; }"
        "QSlider::handle:horizontal { width: 18px; margin: -6px 0; border-radius: 9px; background: #ff4757; border: 2px solid #ffffff; }");
    m_time = new QLabel("00:00.000 / 00:00.000");
    m_modeLabel = new QLabel(this); m_modeLabel->hide();
    auto mk = [&](QStyle::StandardPixmap ic, const QString &tip) { auto *b = new QToolButton; b->setIcon(style()->standardIcon(ic)); b->setToolTip(tip); b->setAutoRaise(true); b->setIconSize(QSize(18, 18)); return b; };
    m_play = mk(QStyle::SP_MediaPlay, tr("Play / Pause (Space)"));
    auto *stopB = mk(QStyle::SP_MediaStop, tr("Stop and return to start (also cancels a preview render)"));
    auto *prev = mk(QStyle::SP_MediaSeekBackward, tr("Previous frame (Left)"));
    auto *next = mk(QStyle::SP_MediaSeekForward, tr("Next frame (Right)"));
    m_srcBtn = new QToolButton; m_srcBtn->setText(tr("Source")); m_srcBtn->setCheckable(true); m_srcBtn->setToolTip(tr("Preview the selected library item"));
    m_tlBtn = new QToolButton; m_tlBtn->setText(tr("Timeline")); m_tlBtn->setCheckable(true); m_tlBtn->setChecked(true); m_tlBtn->setToolTip(tr("Preview the timeline"));
    m_live = new QToolButton; m_live->setText(tr("Live")); m_live->setCheckable(true);
    m_live->setChecked(AppSettings::s().value("preview/live", true).toBool());
    m_live->setToolTip(tr("On (recommended): instant playback while you edit – no rendering. All tracks play; colour effects, transitions and overlays are not shown.\n"
                          "Off: Play renders an exact preview first (slow for long projects)."));
    auto *rend = new QPushButton(tr("Render"));
    rend->setToolTip(tr("Optional: render an exact low-resolution preview of the whole timeline (all effects, transitions, titles, mix) and play it (F5)"));
    auto *markIn = new QToolButton; markIn->setText(tr("In")); markIn->setToolTip(tr("Mark In: start of the part of the source you want to use"));
    auto *markOut = new QToolButton; markOut->setText(tr("Out")); markOut->setToolTip(tr("Mark Out: end of the part of the source you want to use"));
    auto *addB = new QToolButton; addB->setText(tr("Add to timeline")); addB->setToolTip(tr("Add the source (or the marked part) at the playhead"));

    m_master = AppSettings::s().value("preview/volume", 100).toInt() / 100.0;
    m_audio->setVolume(float(mvol(1.0)));
    {
        const QColor ic = Theme::isDark() ? QColor(220, 222, 228) : QColor(40, 42, 48);
        m_volBox = new VolumeBox(ic); m_volBox->setVolume(volumePercent());
        m_abBtn = new QToolButton; m_abBtn->setObjectName("abBtn"); m_abBtn->setText("A B");
    }
    setStyleSheet("QToolButton, QPushButton { font-size: 8pt; padding: 2px 5px; }"
                  "QToolButton#abBtn { border: 1px solid #888; border-radius: 3px; font-weight: bold; padding: 2px 6px; }"
                  "QToolButton[abState=\"1\"] { background: #b8860b; color: white; } QToolButton[abState=\"2\"] { background: #2d9d4a; color: white; }");
    m_time->setStyleSheet("font-size: 8pt;");
    auto *lay = new QVBoxLayout(this); lay->setContentsMargins(0, 0, 0, 0); lay->setSpacing(1);
    lay->addWidget(m_view, 1);
    lay->addWidget(m_slider);
    auto *row = new QHBoxLayout; row->setContentsMargins(4, 0, 4, 2); row->setSpacing(2);      // everything on ONE line
    row->addWidget(prev); row->addWidget(m_play); row->addWidget(stopB); row->addWidget(next); row->addWidget(m_time); row->addWidget(m_abBtn); row->addWidget(m_volBox);
    row->addStretch(1);
    m_srcTools = new QWidget; auto *st = new QHBoxLayout(m_srcTools); st->setContentsMargins(0, 0, 0, 0); st->setSpacing(2);
    st->addWidget(markIn); st->addWidget(markOut); st->addWidget(addB);
    row->addWidget(m_srcTools);
    row->addWidget(m_srcBtn); row->addWidget(m_tlBtn); row->addWidget(m_live); row->addWidget(rend);
    lay->addLayout(row);
    setMinimumWidth(10);

    connect(m_play, &QToolButton::clicked, this, &PreviewMonitor::togglePlay);
    connect(stopB, &QToolButton::clicked, this, &PreviewMonitor::stop);
    connect(prev, &QToolButton::clicked, this, [this] { stepFrame(-1); });
    connect(next, &QToolButton::clicked, this, [this] { stepFrame(1); });
    connect(rend, &QPushButton::clicked, this, &PreviewMonitor::renderPreviewRequested);
    connect(m_live, &QToolButton::toggled, this, [this](bool on) {
        AppSettings::s().setValue("preview/live", on);
        pause(); if (!on) releaseLive();
        setMode(m_mode);
    });
    connect(m_srcBtn, &QToolButton::clicked, this, [this] {
        const QString id = m->selectedMedia().isEmpty() ? m_sourceId : m->selectedMedia();
        if (!id.isEmpty()) playSource(id); else { m_srcBtn->setChecked(m_mode == Mode::Source); emit message(tr("Select an item in the media library first.")); }
    });
    connect(m_tlBtn, &QToolButton::clicked, this, [this] {
        pause(); setMode(Mode::Timeline); setCanvas(m->project().settings.width, m->project().settings.height);
        m_loadedPath.clear(); m_activeClipId.clear(); syncTo(m->playhead(), false);
    });
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
    m_view->viewport()->installEventFilter(this);                          // double-click = full screen; keeps the picture fitted
    connect(m_volBox, &VolumeBox::volumeChanged, this, &PreviewMonitor::setMasterVolume);
    connect(m_volBox, &VolumeBox::muteToggled, this, &PreviewMonitor::setMuted);
    connect(m_abBtn, &QToolButton::clicked, this, &PreviewMonitor::abClick);
    m_abTimer.setTimerType(Qt::PreciseTimer); m_abTimer.setInterval(15); connect(&m_abTimer, &QTimer::timeout, this, &PreviewMonitor::abTick);
    styleAb(m_abBtn);
    m_srcTimer.setInterval(120); connect(&m_srcTimer, &QTimer::timeout, this, &PreviewMonitor::syncSourceAudio);
    m_fsSync.setInterval(100);
    connect(&m_fsSync, &QTimer::timeout, this, [this] {
        if (!m_fs) return;
        if (!m_sliderDrag) { QSignalBlocker b(m_fs->slider); m_fs->slider->setValue(m_slider->value()); }
        m_fs->time->setText(m_time->text());
        m_fs->play->setIcon(uiIcon(isPlaying() ? "pause" : "play", QColor(245, 245, 248)));
    });
    connect(m_slider, &SeekSlider::dragStateChanged, this, [this](bool d) { m_sliderDrag = d; });
    connect(m_slider, &SeekSlider::seekRequested, this, &PreviewMonitor::onSeek);
    wire(m_player); wire(m_pre.pl);
    connect(m_video, &QGraphicsVideoItem::nativeSizeChanged, this, [this] { fit(); });
    m_timer.setTimerType(Qt::PreciseTimer); m_timer.setInterval(16);
    m_smooth.setTimerType(Qt::PreciseTimer); m_smooth.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &PreviewMonitor::tick);
    connect(&m_smooth, &QTimer::timeout, this, &PreviewMonitor::smoothTick);
    connect(m, &ProjectModel::playheadChanged, this, &PreviewMonitor::onPlayheadChanged);
    connect(m, &ProjectModel::changed, this, &PreviewMonitor::onProjectChanged);
    setMode(Mode::Timeline);
    setCanvas(m->project().settings.width, m->project().settings.height);
}

void PreviewMonitor::wire(QMediaPlayer *pl) {
    connect(pl, &QMediaPlayer::positionChanged, this, [this, pl](qint64 p) { onPosition(pl, p); });
    connect(pl, &QMediaPlayer::playbackStateChanged, this, [this, pl](QMediaPlayer::PlaybackState s) { onState(pl, s); });
    connect(pl, &QMediaPlayer::mediaStatusChanged, this, [this, pl](QMediaPlayer::MediaStatus s) { onStatus(pl, s); });
}

void PreviewMonitor::onPosition(QMediaPlayer *pl, qint64 pos) {
    if (pl == m_player) {
        m_lastReported = pos; m_lastClock.restart();
        if (m_mode == Mode::Source) {
            if (!m_sliderDrag && m_player->duration() > 0) { QSignalBlocker b(m_slider); m_slider->setValue(int(10000.0 * pos / m_player->duration())); }
            updateTimeLabel();
        }
    } else if (pl == m_pre.pl) m_pre.lastPos = pos;
}
void PreviewMonitor::onState(QMediaPlayer *pl, QMediaPlayer::PlaybackState s) {
    if (pl != m_player) return;
    if (m_mode != Mode::Timeline) m_play->setIcon(style()->standardIcon(s == QMediaPlayer::PlayingState ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    if (m_mode == Mode::Source) { if (s == QMediaPlayer::PlayingState) m_srcTimer.start(); else { m_srcTimer.stop(); syncSourceAudio(); } }
    if (m_mode == Mode::Rendered && s == QMediaPlayer::PlayingState) { m_lastReported = m_player->position(); m_lastClock.restart(); m_smooth.start(); }
    else if (m_mode == Mode::Rendered) m_smooth.stop();
}
void PreviewMonitor::onStatus(QMediaPlayer *pl, QMediaPlayer::MediaStatus st) {
    const bool ok = st == QMediaPlayer::LoadedMedia || st == QMediaPlayer::BufferedMedia || st == QMediaPlayer::BufferingMedia;
    if (pl == m_player) {
        if (ok && m_pending.valid) { m_pending.valid = false; applyToPlayer(m_pending.posMs, m_pending.play, m_pending.rate, m_pending.vol); }
        if (st == QMediaPlayer::InvalidMedia) emit message(tr("This file can't be previewed (unsupported or damaged). Exporting may still work."));
    } else if (pl == m_pre.pl) {
        if (ok && m_pre.pend.valid) {
            m_pre.pend.valid = false; m_pre.loaded = true;
            m_pre.pl->setPlaybackRate(std::clamp(m_pre.pend.rate, 0.1, 16.0)); m_pre.pl->setPosition(m_pre.pend.posMs); m_pre.pl->pause();
        }
    } else {
        for (Voice &v : m_voices) if (v.pl == pl && ok && v.pend.valid) {
            v.pend.valid = false; v.loaded = true;
            v.pl->setPlaybackRate(std::clamp(v.pend.rate, 0.1, 16.0)); v.ao->setVolume(float(mvol(v.pend.vol)));
            v.pl->setPosition(v.pend.posMs);
            if (v.pend.play) v.pl->play(); else v.pl->pause();
        }
    }
}

bool PreviewMonitor::eventFilter(QObject *o, QEvent *e) {
    if (o == m_view->viewport()) {
        if (e->type() == QEvent::Resize) fit();                                          // the picture always fills the view (normal and full screen)
        else if (e->type() == QEvent::MouseMove && m_fs) m_fs->showBar();
        else if (e->type() == QEvent::MouseButtonDblClick) { toggleFullscreen(); return true; }   // double-click the picture: full screen on / off
    }
    return QWidget::eventFilter(o, e);
}

void PreviewMonitor::updateBadge() {
    if (m_mode != Mode::Source || std::fabs(m_srcSpeed - 1.0) < 1e-6) { m_badge->setVisible(false); return; }
    QFont f; f.setBold(true); f.setPixelSize(std::max(16, m_ch / 14));
    m_badge->setFont(f); m_badge->setText(QString("%1×").arg(QString::number(m_srcSpeed, 'f', 1)));
    m_badge->setPos(m_cw - m_badge->boundingRect().width() - m_cw * 0.02, m_ch * 0.02);
    m_badge->setVisible(true);
}

void PreviewMonitor::setSourceSpeed(double s) {
    m_srcSpeed = std::clamp(std::round(s * 100) / 100.0, 0.1, 16.0);
    if (m_mode == Mode::Source) { m_player->setPlaybackRate(m_srcSpeed); m_srcSettle.restart(); if (m_pitch->active()) stopPitch(); syncSourceAudio(); }
    updateBadge();
}

// Source playback at another speed: the player's own speed change shifts the pitch, so the sound goes through the pitch-preserving stream.
void PreviewMonitor::syncSourceAudio() {
    if (m_mode != Mode::Source) return;
    const MediaItem *mi = m->project().mediaById(m_sourceId);
    const bool need = m_player->playbackState() == QMediaPlayer::PlayingState && std::fabs(m_srcSpeed - 1.0) > 1e-6 && mi && mi->hasAudio && !mi->missing;
    if (!need) { if (m_pitch->active()) stopPitch(); m_audio->setVolume(float(mvol(1.0))); return; }
    constexpr double kLatency = 0.28;
    const double srcNow = m_player->position() / 1000.0;
    bool restart = m_pitchClipId != "source" || std::fabs(m_pitchSpeed - m_srcSpeed) > 1e-6 || !m_pitch->active();
    if (!restart) {
        const double e = m_pitchClock.elapsed() / 1000.0;
        if (e > kLatency + 0.4) { const double predicted = m_pitchStartSrc + (e - kLatency) * m_srcSpeed; if (std::fabs(predicted - srcNow) > 0.7 * std::max(1.0, m_srcSpeed)) restart = true; }
    }
    if (restart && m_srcSettle.isValid() && m_srcSettle.elapsed() < 180) { if (m_pitch->active()) stopPitch(); m_audio->setVolume(0.0); return; }   // speed just changed: restart the sound when it settles
    if (restart) {
        m_pitchStartSrc = srcNow + kLatency * m_srcSpeed;
        if (!m_pitch->start(mi->path, m_pitchStartSrc, m_srcSpeed, mvol(1.0))) { stopPitch(); m_audio->setVolume(float(mvol(1.0))); return; }
        m_pitchClock.start(); m_pitchClipId = "source"; m_pitchSpeed = m_srcSpeed;
    }
    m_pitch->setVolume(mvol(1.0));
    m_audio->setVolume(0.0);
}

void PreviewMonitor::toggleFullscreen() { if (m_fs) exitFullscreen(); else enterFullscreen(); }
void PreviewMonitor::enterFullscreen() {
    if (m_fs) return;
    static_cast<QBoxLayout *>(layout())->removeWidget(m_view);
    m_fs = new FullscreenWindow(m_view);
    connect(m_fs, &FullscreenWindow::exitRequested, this, &PreviewMonitor::exitFullscreen);
    connect(m_fs, &FullscreenWindow::togglePlayRequested, this, &PreviewMonitor::togglePlay);
    connect(m_fs->slider, &SeekSlider::seekRequested, this, &PreviewMonitor::onSeek);
    connect(m_fs->slider, &SeekSlider::dragStateChanged, this, [this](bool d) { m_sliderDrag = d; });
    connect(m_fs->abBtn, &QToolButton::clicked, this, &PreviewMonitor::abClick);
    connect(m_fs->vol, &VolumeBox::volumeChanged, this, &PreviewMonitor::setMasterVolume);
    connect(m_fs->vol, &VolumeBox::muteToggled, this, &PreviewMonitor::setMuted);
    m_fs->vol->setVolume(volumePercent()); m_fs->vol->setMuted(m_muted);
    if (QScreen *sc = window()->screen()) m_fs->setGeometry(sc->geometry());       // on the screen the editor is on
    m_fs->showFullScreen(); m_view->show(); m_fs->setFocus(); m_fs->showBar();
    updateAbUi();
    m_fsSync.start();
}
void PreviewMonitor::exitFullscreen() {
    if (!m_fs) return;
    m_fsSync.stop();
    FullscreenWindow *w = m_fs; m_fs = nullptr;
    m_view->viewport()->unsetCursor();
    m_view->setBackgroundBrush(QColor(10, 10, 12));
    m_view->setParent(this);                                                       // the view goes back into the monitor
    static_cast<QBoxLayout *>(layout())->insertWidget(0, m_view, 1);
    m_view->show();
    w->hide(); w->deleteLater();
    fit();
}

void PreviewMonitor::showEvent(QShowEvent *) { fit(); }
void PreviewMonitor::resizeEvent(QResizeEvent *) { fit(); }
void PreviewMonitor::setCanvas(int w, int h) {
    m_cw = std::max(16, w); m_ch = std::max(16, h);
    m_canvas->setRect(0, 0, m_cw, m_ch);
    m_video->setSize(QSizeF(m_cw, m_ch)); m_pre.vi->setSize(QSizeF(m_cw, m_ch));
    m_scene->setSceneRect(0, 0, m_cw, m_ch);
    fit();
}
void PreviewMonitor::fit() { m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio); }

void PreviewMonitor::showBusy(const QString &text) {
    if (text.isEmpty()) { m_busy->setVisible(false); return; }
    QFont f; f.setBold(true); f.setPixelSize(std::max(14, m_ch / 16));
    m_busy->setFont(f); m_busy->setText(text);
    const QRectF br = m_busy->boundingRect();
    m_busy->setPos((m_cw - br.width()) / 2, (m_ch - br.height()) / 2);
    m_busy->setVisible(true);
}

void PreviewMonitor::updateLiveNote() {
    bool fx = false;
    if (m_mode == Mode::Timeline && m_live->isChecked())
        for (const Clip &c : m->project().clips) {
            if (c.isTitle()) continue;
            const ClipEffects &e = c.fx;
            if (e.rotation || e.flipH || e.flipV || e.cropL + e.cropR + e.cropT + e.cropB > 0 || std::fabs(e.brightness) > 1e-6 || std::fabs(e.contrast - 1) > 1e-6 ||
                std::fabs(e.saturation - 1) > 1e-6 || e.sharpness != 0 || e.opacity < 0.999 || std::fabs(e.scale - 1) > 1e-6 || e.transition != "none" || e.fadeIn > 0 || e.fadeOut > 0) { fx = true; break; }
        }
    if (!fx) { if (m_note->isVisible()) m_note->setVisible(false); return; }
    QFont f; f.setPixelSize(std::max(10, m_ch / 38));
    m_note->setFont(f); m_note->setText(tr("Live preview is approximate – effects, transitions and overlays are not shown. Press Render for the exact result."));
    m_note->setPos(m_cw * 0.015, m_ch - m_note->boundingRect().height() - m_ch * 0.015);
    m_note->setVisible(true);
}

void PreviewMonitor::setMode(Mode md) {
    const Mode oldMode = m_mode;
    m_mode = md;
    if ((md == Mode::Source) != (oldMode == Mode::Source)) abClear();          // source time and timeline time are different scales
    m_srcBtn->setChecked(md == Mode::Source);
    m_tlBtn->setChecked(md != Mode::Source);
    m_srcTools->setVisible(md == Mode::Source);
    m_live->setEnabled(md != Mode::Source);
    m_modeLabel->setText(md == Mode::Source ? tr("SOURCE") : md == Mode::Rendered ? tr("TIMELINE – rendered preview") : (m_live->isChecked() ? tr("TIMELINE – live (approximate)") : tr("TIMELINE")));
    m_time->setToolTip(m_modeLabel->text());
    if (md != Mode::Timeline) {
        for (auto *i : m_titleItems) { m_scene->removeItem(i); delete i; }
        m_titleItems.clear(); m_titleKey.clear();
        releaseLive();
    }
    if (md != Mode::Rendered) m_smooth.stop();
    if (md != Mode::Source) m_srcTimer.stop();
    updateLiveNote(); updateBadge();
    emit modeChanged();
}

void PreviewMonitor::stopPitch() { if (m_pitch) m_pitch->stop(); m_pitchClipId.clear(); m_pitchSpeed = 0; }

void PreviewMonitor::releaseLive() {
    stopPitch();
    if (m_pre.pl) { m_pre.pl->pause(); m_pre.pl->setSource(QUrl()); }
    m_pre.clipId.clear(); m_pre.path.clear(); m_pre.loaded = false; m_pre.pend.valid = false;
    for (Voice &v : m_voices) { v.pl->pause(); v.pl->setSource(QUrl()); v.clipId.clear(); v.path.clear(); v.loaded = false; v.pend.valid = false; }
    m_activeClipId.clear();
}

void PreviewMonitor::releaseMedia() {
    pause(); m_player->setSource(QUrl()); releaseLive();
    m_loadedPath.clear(); m_pending.valid = false; m_sourceId.clear(); showBusy({});
}

void PreviewMonitor::playSource(const QString &mediaId) {
    const MediaItem *mi = m->project().mediaById(mediaId);
    if (!mi) return;
    pause();
    if (mi->missing) { emit message(tr("This file is missing. Use File > Relink Missing Media.")); return; }
    setMode(Mode::Source);
    showBusy({});
    m_srcSpeed = 1.0; updateBadge(); abClear();
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
    m_player->setPlaybackRate(1.0); m_audio->setVolume(float(mvol(1.0)));
    m_player->setSource(QUrl::fromLocalFile(path));
    m_pending = {true, 0, true, 1.0, 1.0};
}

void PreviewMonitor::playRendered(const QString &path, double startSeconds, bool autoplay) {
    pause();
    setMode(Mode::Rendered);
    showBusy({});
    m_srcFps = m->project().settings.fps > 0 ? m->project().settings.fps : 30;
    setCanvas(m->project().settings.width, m->project().settings.height);
    m_image->setVisible(false); m_video->setVisible(true);
    m_loadedPath = path; m_offsetMs = 0; m_endMs = 0; m_renderedSig = m->project().renderSignature();
    m_player->setPlaybackRate(1.0); m_audio->setVolume(float(mvol(1.0)));
    m_player->setSource(QUrl::fromLocalFile(path));
    m_pending = {true, qint64(std::max(0.0, startSeconds) * 1000), autoplay, 1.0, 1.0};
}

void PreviewMonitor::playDirect(const QString &path, double in, double out, double start, bool autoplay) {
    pause();
    setMode(Mode::Rendered);
    showBusy({});
    m_modeLabel->setText(tr("TIMELINE – direct")); m_time->setToolTip(m_modeLabel->text());
    m_srcFps = m->project().settings.fps > 0 ? m->project().settings.fps : 30;
    setCanvas(m->project().settings.width, m->project().settings.height);
    m_image->setVisible(false); m_video->setVisible(true);
    m_loadedPath = path; m_offsetMs = qint64(in * 1000); m_endMs = qint64(out * 1000); m_renderedSig = m->project().renderSignature();
    m_player->setPlaybackRate(1.0); m_audio->setVolume(float(mvol(1.0)));
    m_player->setSource(QUrl::fromLocalFile(path));
    m_pending = {true, m_offsetMs + qint64(std::max(0.0, start) * 1000), autoplay, 1.0, 1.0};
}

qint64 PreviewMonitor::rangeMs() const { return m_endMs > 0 ? m_endMs - m_offsetMs : m_player->duration(); }

bool PreviewMonitor::isPlaying() const { return m_tlPlaying || m_player->playbackState() == QMediaPlayer::PlayingState; }

void PreviewMonitor::togglePlay() {
    if (isPlaying()) { pause(); return; }
    if (m_mode == Mode::Timeline) {
        const double dur = m->project().duration();
        if (dur <= 0) { emit message(tr("The timeline is empty. Add clips first.")); return; }
        if (!m_live->isChecked()) { emit timelinePlayRequested(); return; }
        if (m->playhead() >= dur - 0.05) m->setPlayhead(0);
        m_t0 = m->playhead(); m_clock.start(); m_lastLiveMs = 0; m_tlPlaying = true; m_timer.start();
        m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        syncTo(m_t0, true);
    } else {
        if (m_mode == Mode::Rendered && m_endMs > 0 && m_player->position() >= m_endMs - 50) m_player->setPosition(m_offsetMs);
        else if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) m_player->setPosition(m_offsetMs);
        m_player->play();
    }
}
void PreviewMonitor::pause() {
    if (m_tlPlaying) { m_tlPlaying = false; m_timer.stop(); }
    m_smooth.stop();
    stopPitch();
    if (m_mode == Mode::Source) m_audio->setVolume(float(mvol(1.0)));
    m_player->pause();
    if (m_pre.pl) m_pre.pl->pause();
    for (Voice &v : m_voices) { v.pl->pause(); v.pend.play = false; }
    m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
}
void PreviewMonitor::stop() {
    emit stopPressed();
    pause();
    if (m_mode == Mode::Timeline) m->setPlayhead(0);
    else { m_player->setPosition(m_mode == Mode::Rendered ? m_offsetMs : 0); if (m_mode == Mode::Rendered) m->setPlayhead(0, true); }
}
void PreviewMonitor::stepFrame(int dir) {
    pause();
    if (m_mode == Mode::Timeline) m->setPlayhead(std::max(0.0, m->playhead() + dir / std::max(1.0, m->project().settings.fps)));
    else {
        m_player->setPosition(std::max<qint64>(0, m_player->position() + qint64(dir * 1000.0 / m_srcFps)));
        if (m_mode == Mode::Rendered) m->setPlayhead((m_player->position() - m_offsetMs) / 1000.0, true);
    }
}

void PreviewMonitor::seekRelative(double sec) {
    if (m_mode == Mode::Timeline) { m->setPlayhead(std::clamp(m->playhead() + sec, 0.0, std::max(0.0, m->project().duration()))); return; }
    const qint64 off = m_mode == Mode::Rendered ? m_offsetMs : 0;
    const qint64 rng = std::max<qint64>(0, rangeMs());
    const qint64 pos = std::clamp<qint64>(m_player->position() - off + qint64(sec * 1000), 0, rng);
    m_player->setPosition(off + pos);
    if (m_mode == Mode::Rendered) m->setPlayhead(pos / 1000.0, true);
    updateTimeLabel();
}
void PreviewMonitor::goStart() { seekRelative(-1e9); }
void PreviewMonitor::goEnd() { seekRelative(1e9); }

void PreviewMonitor::onSeek(double f) {
    if (m_mode == Mode::Timeline) {
        m->setPlayhead(f * std::max(0.0, m->project().duration()));
    } else if (rangeMs() > 0) {
        const qint64 off = m_mode == Mode::Rendered ? m_offsetMs : 0;
        m_player->setPosition(off + qint64(f * rangeMs()));
        if (m_mode == Mode::Rendered) m->setPlayhead(f * rangeMs() / 1000.0, true);
        updateTimeLabel();
    }
}

const Clip *PreviewMonitor::activeVideoClip(double t, bool hiddenTracks) const {
    const Project &p = m->project();
    for (int i = p.tracks.size() - 1; i >= 0; --i) {        // later video tracks are drawn on top
        const Track &tr = p.tracks[i];
        if (tr.type != TrackType::Video || p.trackVisible(tr) == hiddenTracks) continue;    // hiddenTracks: look at tracks whose picture is hidden
        for (const Clip &c : p.clips) {
            if (c.trackId != tr.id || c.isTitle()) continue;
            const MediaItem *mi = p.mediaById(c.mediaId);
            if (mi && mi->hasVideo && t >= c.start - 1e-6 && t < c.end() - 1e-6) return &c;
        }
    }
    return nullptr;
}

const Clip *PreviewMonitor::nextVideoClip(double after) const {
    const Project &p = m->project();
    const Clip *best = nullptr;
    for (const Track &tr : p.tracks) {
        if (tr.type != TrackType::Video || !p.trackVisible(tr)) continue;
        for (const Clip &c : p.clips) {
            if (c.trackId != tr.id || c.isTitle()) continue;
            const MediaItem *mi = p.mediaById(c.mediaId);
            if (!mi || mi->kind != "video" || mi->missing) continue;
            if (c.start > after + 0.001 && c.start <= after + kLookahead && (!best || c.start < best->start)) best = &c;
        }
    }
    return best;
}

void PreviewMonitor::applyToPlayer(qint64 posMs, bool play, double rate, double vol) {
    m_activeRate = rate;
    m_player->setPlaybackRate(std::clamp(rate, 0.1, 16.0));                       // rate first ...
    if (std::llabs(m_player->position() - posMs) > (play ? 350 : 20)) m_player->setPosition(posMs);   // ... then the position, so a rate change can't leave the picture elsewhere
    m_audio->setVolume(float(mvol(vol)));
    if (play) m_player->play(); else m_player->pause();
}

void PreviewMonitor::keepPlaying(qint64 target) {
    if (m_pending.valid) { m_pending.posMs = target; m_pending.play = true; return; }
    if (m_player->playbackState() != QMediaPlayer::PlayingState) m_player->play();
    if (std::llabs(m_player->position() - target) > 450) m_player->setPosition(target);
}

void PreviewMonitor::loadActive(const Clip &c, const MediaItem &mi, qint64 target, bool play, double rate, double vol) {
    const QString path = m->playablePath(mi.id, AppSettings::useProxies());
    m_activeClipId = c.id; m_activeRate = rate;
    if (m_loadedPath != path) {
        m_loadedPath = path;
        m_pending = {true, target, play, rate, vol};
        m_player->setSource(QUrl::fromLocalFile(path));
        m_audio->setVolume(float(mvol(vol)));
        m_player->setPlaybackRate(std::clamp(rate, 0.1, 16.0));
    } else if (m_pending.valid) {
        m_pending = {true, target, play, rate, vol};
    } else {
        applyToPlayer(target, play, rate, vol);
    }
}

void PreviewMonitor::preloadDeck(const Clip &c) {
    const MediaItem *mi = m->project().mediaById(c.mediaId);
    if (!mi || mi->kind != "video" || mi->missing) return;
    const QString path = m->playablePath(mi->id, AppSettings::useProxies());
    m_pre.clipId = c.id;
    m_pre.pend = {true, qint64(c.in * 1000), false, c.fx.speed, 1.0};
    if (m_pre.path != path) { m_pre.path = path; m_pre.loaded = false; m_pre.pl->setSource(QUrl::fromLocalFile(path)); }
    else if (m_pre.loaded) { m_pre.pend.valid = false; m_pre.pl->setPlaybackRate(std::clamp(c.fx.speed, 0.1, 16.0)); m_pre.pl->setPosition(m_pre.pend.posMs); m_pre.pl->pause(); }
}

void PreviewMonitor::swapDecks() {                  // gap-free cut: the preloaded deck becomes the active one
    std::swap(m_player, m_pre.pl); std::swap(m_audio, m_pre.ao); std::swap(m_video, m_pre.vi);
    std::swap(m_loadedPath, m_pre.path); std::swap(m_pending, m_pre.pend); std::swap(m_lastReported, m_pre.lastPos);
    const QString old = m_activeClipId; m_activeClipId = m_pre.clipId; m_pre.clipId = old;
    m_pre.loaded = true;
    m_video->setVisible(true); m_pre.vi->setVisible(false);
    m_pre.pl->pause();
    m_lastClock.restart();
}

void PreviewMonitor::ensureVoices() {
    if (!m_voices.isEmpty()) return;
    for (int i = 0; i < 6; ++i) {
        Voice v; v.pl = new QMediaPlayer(this); v.ao = new QAudioOutput(this); v.pl->setAudioOutput(v.ao);
        wire(v.pl); m_voices.push_back(v);
    }
}

// Faster/slower clips: the player's own speed change shifts the pitch, so the clip's sound is streamed through ffmpeg's atempo
// (pitch preserved) instead and the deck's own audio is muted.
double PreviewMonitor::pitchAudioFor(const Clip &c, const MediaItem &mi, double t, bool play, double vol) {
    const bool need = play && std::fabs(c.fx.speed - 1.0) > 1e-6 && c.fx.preservePitch && vol > 0 && !c.fx.reverse && mi.hasAudio;
    if (!need) { if (m_pitch->active()) stopPitch(); return vol; }
    const double srcNow = c.in + (t - c.start) * c.fx.speed;
    constexpr double kLatency = 0.28;                         // seconds until the first audio arrives
    if (std::fabs(c.fx.speed - m_pitchSpeedSeen) > 1e-6) { m_pitchSpeedSeen = c.fx.speed; m_pitchSeenClock.restart(); }
    bool restart = m_pitchClipId != c.id || std::fabs(m_pitchSpeed - c.fx.speed) > 1e-6 || !m_pitch->active();
    if (!restart) {
        const double e = m_pitchClock.elapsed() / 1000.0;
        if (e > kLatency + 0.4) { const double predicted = m_pitchStartSrc + (e - kLatency) * c.fx.speed; if (std::fabs(predicted - srcNow) > 0.7 * std::max(1.0, c.fx.speed)) restart = true; }
    }
    if (restart && m_pitchSeenClock.elapsed() < 180) { if (m_pitch->active()) stopPitch(); return 0.0; }       // speed is still changing (key held / repeated): stay quiet, restart the sound once it settles
    if (restart) {
        m_pitchStartSrc = srcNow + kLatency * c.fx.speed;
        if (!m_pitch->start(mi.path, m_pitchStartSrc, c.fx.speed, mvol(vol))) { stopPitch(); return vol; }   // no device: fall back to the player's audio
        m_pitchClock.start(); m_pitchClipId = c.id; m_pitchSpeed = c.fx.speed;
    } else m_pitch->setVolume(mvol(vol));
    return 0.0;
}

void PreviewMonitor::syncVoices(double t, bool play) {
    if (!play) { for (Voice &v : m_voices) { v.pl->pause(); v.pend.play = false; } return; }
    ensureVoices();
    const Project &p = m->project();
    QStringList wanted;
    struct W { const Clip *c; const Track *t; const MediaItem *mi; };
    QVector<W> list;
    for (const Track &tr : p.tracks) {
        if (!isAudioType(tr.type) || !p.trackAudible(tr)) continue;
        for (const Clip &c : p.clips) {
            if (c.trackId != tr.id || c.fx.mute) continue;
            const MediaItem *mi = p.mediaById(c.mediaId);
            if (!mi || !mi->hasAudio || mi->missing) continue;
            if (t >= c.start - kLookahead && t < c.end() - 1e-3) { list.push_back({&c, &tr, mi}); wanted << c.id; }
        }
    }
    for (Voice &v : m_voices)                             // free voices whose clip is no longer needed
        if (!v.clipId.isEmpty() && !wanted.contains(v.clipId)) { v.pl->pause(); v.pl->setSource(QUrl()); v.clipId.clear(); v.path.clear(); v.loaded = false; v.pend.valid = false; }
    for (const W &w : list) {
        const Clip &c = *w.c;
        Voice *v = nullptr;
        for (Voice &x : m_voices) if (x.clipId == c.id) v = &x;
        const bool started = t >= c.start;
        const qint64 target = qint64((c.in + std::max(0.0, t - c.start) * c.fx.speed) * 1000.0);
        const double vol = c.fx.volume * w.t->volume, rate = c.fx.speed;
        if (!v) {
            for (Voice &x : m_voices) if (x.clipId.isEmpty()) { v = &x; break; }
            if (!v) continue;
            v->clipId = c.id; v->path = m->playablePath(w.mi->id, AppSettings::useProxies()); v->loaded = false;
            v->pend = {true, target, started, rate, vol};
            v->pl->setSource(QUrl::fromLocalFile(v->path));
            continue;
        }
        if (v->pend.valid) { v->pend.posMs = target; v->pend.play = started; continue; }
        v->ao->setVolume(float(mvol(vol)));
        if (started) {
            if (v->pl->playbackState() != QMediaPlayer::PlayingState) { v->pl->setPosition(target); v->pl->play(); }
            else if (std::llabs(v->pl->position() - target) > 450) v->pl->setPosition(target);
        } else if (v->pl->playbackState() == QMediaPlayer::PlayingState) v->pl->pause();
    }
}

void PreviewMonitor::syncTo(double t, bool play) {
    bool pictureShown = true;
    const Clip *c = activeVideoClip(t);
    if (!c) { c = activeVideoClip(t, true); pictureShown = false; }       // picture hidden (H): keep decoding so the sound continues
    updateTitles(t);
    updateTimeLabel();
    updateLiveNote();
    if (!c) {
        stopPitch();
        m_player->pause(); m_video->setVisible(false); m_image->setVisible(false); m_activeClipId.clear();
    } else {
        const MediaItem *mi = m->project().mediaById(c->mediaId);
        if (!mi || mi->missing) { m_player->pause(); m_video->setVisible(false); }
        else if (mi->kind == "image") {
            m_player->pause(); m_video->setVisible(false); m_activeClipId.clear();
            if (!pictureShown) { m_image->setVisible(false); }
            else if (m_activeImageId != mi->id) {
                m_activeImageId = mi->id;
                QPixmap pm(mi->path); m_image->setPixmap(pm);
                m_image->setScale(std::min(double(m_cw) / std::max(1, pm.width()), double(m_ch) / std::max(1, pm.height())));
            }
            if (pictureShown) m_image->setVisible(true);
        } else {
            m_image->setVisible(false); m_video->setVisible(pictureShown);
            const Track *tr = m->project().trackById(c->trackId);
            const qint64 target = qint64((c->in + (t - c->start) * c->fx.speed) * 1000.0);
            const bool audible = tr && m->project().trackAudible(*tr);                  // track mute / solo
            const double ownVol = (c->fx.mute || !audible) ? 0.0 : c->fx.volume * (tr ? tr->volume : 1.0);
            const double vol = pictureShown || true ? pitchAudioFor(*c, *mi, t, play, ownVol) : ownVol;       // 0 while the pitch-preserving stream plays
            if (m_activeClipId == c->id) {
                if (play) {
                    if (std::fabs(m_activeRate - c->fx.speed) > 1e-6 && !m_pending.valid) { m_player->setPlaybackRate(std::clamp(c->fx.speed, 0.1, 16.0)); m_activeRate = c->fx.speed; if (std::llabs(m_player->position() - target) > 150) m_player->setPosition(target); m_lastReported = m_player->position(); m_lastClock.restart(); }   // speed changed while playing: only re-seek if the picture really moved
                    m_audio->setVolume(float(mvol(vol))); keepPlaying(target);
                } else if (m_pending.valid) { m_pending.posMs = target; m_pending.play = false; } else applyToPlayer(target, false, c->fx.speed, vol);
            } else if (play && m_pre.clipId == c->id && m_pre.loaded) {
                swapDecks();                                   // next clip was preloaded: switch with no gap
                m_activeClipId = c->id;
                applyToPlayer(target, true, c->fx.speed, vol);
            } else {
                loadActive(*c, *mi, target, play, c->fx.speed, vol);
            }
            if (play) {                                       // get the following clip ready in the other deck
                const Clip *n = nextVideoClip(t);
                if (n && n->id != m_pre.clipId && n->id != c->id) preloadDeck(*n);
            }
        }
    }
    syncVoices(t, play);
}

void PreviewMonitor::updateTitles(double t) {
    QByteArray key;
    const Project &p = m->project();
    QVector<const Clip *> active;
    if (m_mode == Mode::Timeline)
        for (const Track &tr : p.tracks) {
            if (tr.type != TrackType::Title || !p.trackVisible(tr)) continue;
            for (const Clip &c : p.clips) if (c.trackId == tr.id && t >= c.start && t < c.end()) { active.push_back(&c); key += c.id.toUtf8() + QJsonDocument(c.title.toJson()).toJson(QJsonDocument::Compact); }
        }
    if (key == m_titleKey.toUtf8() && (!active.isEmpty() || m_titleItems.isEmpty())) return;     // nothing changed: don't rebuild
    m_titleKey = QString::fromUtf8(key);
    for (auto *i : m_titleItems) { m_scene->removeItem(i); delete i; }
    m_titleItems.clear();
    for (const Clip *cp : active) {
        const Clip &c = *cp;
        auto *txt = new QGraphicsSimpleTextItem(c.title.caps ? c.title.text.toUpper() : c.title.text);
        QFont f; if (!c.title.fontFamily.isEmpty()) f.setFamily(c.title.fontFamily);
        f.setBold(c.title.bold); f.setItalic(c.title.italic); f.setPixelSize(std::max(8, int(c.title.fontSize * m_ch / 1080.0)));
        txt->setFont(f);
        QColor col(c.title.color); col.setAlphaF(c.title.textOpacity); txt->setBrush(col);
        if (c.title.outline) txt->setPen(QPen(QColor(c.title.outlineColor), std::max(1.0, c.title.outlineWidth * m_ch / 1080.0)));
        const QRectF br = txt->boundingRect();
        const double pad = c.title.box ? f.pixelSize() * 0.3 : 0;
        const double x = (m_cw - br.width() - 2 * pad) * c.title.posX + pad, y = (m_ch - br.height() - 2 * pad) * c.title.posY + pad;
        if (c.title.box) {
            QColor bc(c.title.boxColor); bc.setAlphaF(c.title.boxOpacity);
            auto *box = m_scene->addRect(QRectF(x - pad, y - pad, br.width() + 2 * pad, br.height() + 2 * pad), Qt::NoPen, bc);
            box->setZValue(10); m_titleItems.push_back(box);
        }
        if (c.title.shadow) {
            auto *sh = new QGraphicsSimpleTextItem(txt->text()); sh->setFont(f);
            QColor sc(c.title.shadowColor); sc.setAlphaF(c.title.shadowOpacity); sh->setBrush(sc);
            sh->setPos(x + c.title.shadowX * m_ch / 1080.0, y + c.title.shadowY * m_ch / 1080.0); sh->setZValue(10.5);
            m_scene->addItem(sh); m_titleItems.push_back(sh);
        }
        txt->setPos(x, y); txt->setZValue(11);
        m_scene->addItem(txt); m_titleItems.push_back(txt);
    }
}

double PreviewMonitor::liveTime() {
    double wall = m_t0 + m_clock.elapsed() / 1000.0;
    const Clip *c = m_activeClipId.isEmpty() ? nullptr : m->project().clipById(m_activeClipId);
    if (c && !m_pending.valid && m_player->playbackState() == QMediaPlayer::PlayingState) {
        const qint64 reported = m_player->position();                   // the video clock is the master: the playhead follows the picture
        if (reported != m_lastReported) { m_lastReported = reported; m_lastClock.restart(); }
        double est = double(m_lastReported) + double(m_lastClock.elapsed()) * c->fx.speed;
        if (std::fabs(est - reported) > 250.0 * std::max(1.0, c->fx.speed)) est = double(reported);
        const double t = c->start + (est / 1000.0 - c->in) / std::max(0.01, c->fx.speed);
        const double err = t - wall;
        if (std::fabs(err) < 0.6) {        // steer the clock gently toward the picture: the playhead never jumps and never runs backwards
            const double dtTick = std::max(0.001, double(m_clock.elapsed() - m_lastLiveMs) / 1000.0);
            const double corr = std::clamp(err * 0.1, -0.5 * dtTick, 2.0 * dtTick);
            m_t0 += corr; wall += corr;
        }
    }
    m_lastLiveMs = m_clock.elapsed();
    return wall;
}

void PreviewMonitor::tick() {
    const double t = liveTime();
    const double dur = m->project().duration();
    if (m_ab == 2 && t >= m_abB) { m->setPlayhead(m_abA); return; }                // A-B repeat: jump back to A
    if (t >= dur) { m->setPlayhead(dur, true); pause(); return; }
    m->setPlayhead(t, true);
    syncTo(t, true);
}

void PreviewMonitor::smoothTick() {                 // rendered / direct playback: interpolate between the player's coarse updates
    if (m_mode != Mode::Rendered || m_player->playbackState() != QMediaPlayer::PlayingState) return;
    const qint64 reported = m_player->position();
    if (reported != m_lastReported) { m_lastReported = reported; m_lastClock.restart(); }
    double estMs = double(m_lastReported) + double(m_lastClock.elapsed());
    if (std::fabs(estMs - reported) > 250) estMs = double(reported);
    if (m_endMs > 0 && estMs >= m_endMs - 20) {
        pause(); m_player->setPosition(m_endMs); m->setPlayhead((m_endMs - m_offsetMs) / 1000.0, true); updateTimeLabel(); return;
    }
    const double t = std::max(0.0, (estMs - m_offsetMs) / 1000.0);
    m->setPlayhead(t, true);
    const qint64 rng = rangeMs();
    if (!m_sliderDrag && rng > 0) { QSignalBlocker b(m_slider); m_slider->setValue(int(10000.0 * (estMs - m_offsetMs) / rng)); }
    m_time->setText(humanTime(t, true) + " / " + humanTime(rng / 1000.0, true));
}

void PreviewMonitor::onPlayheadChanged(double t, bool fromPlayback) {
    if (fromPlayback) return;
    if (m_mode == Mode::Rendered) { m_player->setPosition(m_offsetMs + qint64(t * 1000)); updateTimeLabel(); return; }
    if (m_mode == Mode::Source) { pause(); setMode(Mode::Timeline); setCanvas(m->project().settings.width, m->project().settings.height); m_loadedPath.clear(); }
    if (m_tlPlaying) { m_t0 = t; m_clock.restart(); m_lastLiveMs = 0; }
    syncTo(t, m_tlPlaying);
}

void PreviewMonitor::onProjectChanged() {
    if (m_mode == Mode::Rendered && m->project().renderSignature() == m_renderedSig) { updateTimeLabel(); return; }   // e.g. a marker: nothing to re-render
    if (m_mode == Mode::Rendered) {
        pause(); setMode(Mode::Timeline);
        setCanvas(m->project().settings.width, m->project().settings.height); m_loadedPath.clear();
        emit message(tr("The edit changed – the exact preview is out of date. Live playback works as usual; press Render to refresh the exact preview."));
    }
    if (m_mode == Mode::Timeline && !m_tlPlaying) syncTo(m->playhead(), false);
    updateTimeLabel();
    if (m_ab == 2) updateAbUi();
}

void PreviewMonitor::updateTimeLabel() {
    if (m_mode == Mode::Timeline) {
        const double dur = m->project().duration();
        m_time->setText(humanTime(m->playhead(), true) + " / " + humanTime(dur, true));
        if (!m_sliderDrag) { QSignalBlocker b(m_slider); m_slider->setValue(dur > 0 ? int(10000.0 * m->playhead() / dur) : 0); }
    } else {
        const qint64 off = m_mode == Mode::Rendered ? m_offsetMs : 0;
        m_time->setText(humanTime(std::max<qint64>(0, m_player->position() - off) / 1000.0, true) + " / " + humanTime(rangeMs() / 1000.0, true));
    }
}

// ======================================================================= volume
void PreviewMonitor::applyMasterNow() {
    if (m_mode == Mode::Source) { if (m_pitch->active()) m_pitch->setVolume(mvol(1.0)); else m_audio->setVolume(float(mvol(1.0))); }
    else if (m_mode == Mode::Rendered) m_audio->setVolume(float(mvol(1.0)));
    // live timeline playback applies it on the next tick
    if (m_volBox) { m_volBox->setVolume(volumePercent()); m_volBox->setMuted(m_muted); }
    if (m_fs) { m_fs->vol->setVolume(volumePercent()); m_fs->vol->setMuted(m_muted); }
}
void PreviewMonitor::setMasterVolume(int pct) {
    m_master = std::clamp(pct, 0, 100) / 100.0;
    AppSettings::s().setValue("preview/volume", int(m_master * 100 + 0.5));
    if (pct > 0) m_muted = false;
    applyMasterNow();
}
void PreviewMonitor::setMuted(bool mu) { m_muted = mu; applyMasterNow(); }

// ======================================================================= A-B repeat
double PreviewMonitor::abPosition() const {
    if (m_mode == Mode::Timeline) return m->playhead();
    if (m_mode == Mode::Source) return m_player->position() / 1000.0;
    return (m_player->position() - m_offsetMs) / 1000.0;
}
double PreviewMonitor::abDuration() const {
    if (m_mode == Mode::Timeline) return m->project().duration();
    if (m_mode == Mode::Source) return m_player->duration() / 1000.0;
    return rangeMs() / 1000.0;
}
void PreviewMonitor::abClick() {
    if (m_ab == 0) {
        m_abA = std::max(0.0, abPosition()); m_ab = 1;
        emit message(tr("A–B repeat: start (A) set at %1. Click again to set the end (B).").arg(humanTime(m_abA, true)));
    } else if (m_ab == 1) {
        const double b = abPosition();
        if (b <= m_abA + 0.05) { emit message(tr("Point B must be after point A. Move forward and click again.")); return; }
        m_abB = b; m_ab = 2; m_abTimer.start();
        emit message(tr("Repeating from %1 to %2. Click the A B button again to stop.").arg(humanTime(m_abA, true), humanTime(m_abB, true)));
    } else {
        abClear();
        emit message(tr("A–B repeat stopped."));
    }
    updateAbUi();
}
void PreviewMonitor::abClear() { m_ab = 0; m_abTimer.stop(); if (m_abBtn) updateAbUi(); }
void PreviewMonitor::abTick() {
    if (m_ab != 2 || m_player->playbackState() != QMediaPlayer::PlayingState) return;
    if (m_mode == Mode::Source) { if (m_player->position() >= qint64(m_abB * 1000) - 10) m_player->setPosition(qint64(m_abA * 1000)); }
    else if (m_mode == Mode::Rendered) {
        if (m_player->position() - m_offsetMs >= qint64(m_abB * 1000) - 10) { m_player->setPosition(m_offsetMs + qint64(m_abA * 1000)); m->setPlayhead(m_abA, true); }
    }
}
void PreviewMonitor::styleAb(QToolButton *b) {
    if (!b) return;
    b->setText(m_ab == 1 ? "A ·" : "A B");
    b->setProperty("abState", m_ab);
    b->style()->unpolish(b); b->style()->polish(b);
    b->setToolTip(m_ab == 0 ? tr("A–B repeat: click to mark the START (A) at the current position")
                : m_ab == 1 ? tr("Click again to mark the END (B). The part between A and B will then repeat")
                            : tr("Repeating A–B. Click to stop repeating"));
}
void PreviewMonitor::updateAbUi() {
    const double dur = abDuration();
    double fa = -1, fb = -1;
    if (m_ab >= 1 && dur > 0) { fa = std::clamp(m_abA / dur, 0.0, 1.0); fb = m_ab == 2 ? std::clamp(m_abB / dur, 0.0, 1.0) : fa + 0.002; }
    m_slider->setLoop(fa, fb);
    if (m_fs) m_fs->slider->setLoop(fa, fb);
    styleAb(m_abBtn);
    if (m_fs) styleAb(m_fs->abBtn);
}

} // namespace mv
