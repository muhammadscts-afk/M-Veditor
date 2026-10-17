#include "TimelineView.h"
#include "MediaProbe.h"
#include "Theme.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <functional>

namespace mv {

static const char *kMediaMime = "application/x-mveditor-media";

TimelineView::TimelineView(ProjectModel *model, QWidget *parent) : QAbstractScrollArea(parent), m(model) {
    setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(170);

    // "Add track" button with its branching arrow, above the track headers
    m_addBtn = new QToolButton(viewport());
    m_addBtn->setText(tr("＋ Add track"));
    m_addBtn->setToolTip(tr("Add a new track (video, audio, music, voice-over or titles)"));
    m_addBtn->setPopupMode(QToolButton::InstantPopup);
    m_addBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_addBtn->setStyleSheet("QToolButton { font-weight: bold; border: 1px solid #5a8de0; border-radius: 4px; padding: 2px 8px; }"
                            "QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; right: 4px; }");
    auto *menu = new QMenu(m_addBtn);
    const QList<QPair<QString, int>> types = {{tr("Video track"), int(TrackType::Video)}, {tr("Audio track"), int(TrackType::Audio)},
        {tr("Music track"), int(TrackType::Music)}, {tr("Voice-over track"), int(TrackType::Voice)}, {tr("Title track"), int(TrackType::Title)}};
    for (auto &t : types) menu->addAction(t.first, this, [this, ty = t.second] { emit addTrackRequested(ty); });
    m_addBtn->setMenu(menu);
    m_addBtn->setGeometry(5, 3, kHeaderW - 12, kRulerH - 6);

    connect(m, &ProjectModel::changed, this, [this] { updateScrollBars(); invalidate(); });
    connect(m, &ProjectModel::selectionChanged, this, [this] { invalidate(); });
    connect(m, &ProjectModel::assetsChanged, this, [this] { invalidate(); });
    connect(m, &ProjectModel::playheadChanged, this, [this](double, bool fromPlayback) {
        if (fromPlayback) ensurePlayheadVisible();
        const int nx = int(timeToX(m->playhead()));
        const int x0 = std::min(nx, m_lastPhX) - 14, w = std::abs(nx - m_lastPhX) + 28;   // repaint only the strip that changed
        m_lastPhX = nx;
        viewport()->update(QRect(x0, 0, w, viewport()->height()));
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { invalidate(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { invalidate(); });
    updateScrollBars();
}

void TimelineView::invalidate() { m_dirty = true; viewport()->update(); }

bool TimelineView::compatible(const Clip &c, const MediaItem *media, const Track &t) {
    if (c.isTitle()) return t.type == TrackType::Title;
    if (!media) return false;
    if (media->kind == "audio") return isAudioType(t.type);
    if (media->kind == "image") return t.type == TrackType::Video;
    return t.type == TrackType::Video || (isAudioType(t.type) && media->hasAudio);   // a video may also be used for its sound only
}

QColor TimelineView::regionColor(const Project &p, int index) {
    static const QColor pal[] = {QColor(66, 133, 244), QColor(52, 168, 83), QColor(251, 188, 5), QColor(234, 67, 160), QColor(0, 188, 212), QColor(244, 81, 30)};
    QVector<Marker> mk = p.markers;
    std::sort(mk.begin(), mk.end(), [](const Marker &a, const Marker &b) { return a.time < b.time; });
    QColor c = pal[index % 6];
    if (index >= 1 && index - 1 < mk.size() && !mk[index - 1].color.isEmpty() && QColor(mk[index - 1].color).isValid()) c = QColor(mk[index - 1].color);
    c.setAlpha(Theme::isDark() ? 30 : 38);     // very transparent
    return c;
}

double TimelineView::timeToX(double t) const { return kHeaderW + t * m_pps - horizontalScrollBar()->value(); }
double TimelineView::xToTime(double x) const { return (x - kHeaderW + horizontalScrollBar()->value()) / m_pps; }
int TimelineView::trackTop(int idx) const {
    int y = kRulerH - verticalScrollBar()->value();
    for (int i = 0; i < idx; ++i) y += m->project().tracks[i].height;
    return y;
}
int TimelineView::trackAt(int y) const {
    if (y < kRulerH) return -1;
    for (int i = 0; i < m->project().tracks.size(); ++i) {
        const int top = trackTop(i);
        if (y >= top && y < top + m->project().tracks[i].height) return i;
    }
    return -1;
}
int TimelineView::trackIndexOf(const QString &id) const { return m->project().trackIndex(id); }
QRect TimelineView::clipRect(const Clip &c, int ti) const {
    const int x0 = int(timeToX(c.start)), x1 = int(timeToX(c.end()));
    return QRect(x0, trackTop(ti) + 3, std::max(3, x1 - x0), m->project().tracks[ti].height - 6);
}
QRect TimelineView::headerButton(int idx, int ti) const { return QRect(kHeaderW - 8 - (idx + 1) * 24, trackTop(ti) + 5, 22, 20); }

void TimelineView::updateScrollBars() {
    const double dur = std::max(m->project().duration(), 10.0) + 30.0;
    const int viewW = viewport()->width() - kHeaderW;
    horizontalScrollBar()->setRange(0, std::max(0, int(dur * m_pps) - viewW));
    horizontalScrollBar()->setPageStep(std::max(50, viewW));
    int total = 0;
    for (auto &t : m->project().tracks) total += t.height;
    const int viewH = viewport()->height() - kRulerH;
    verticalScrollBar()->setRange(0, std::max(0, total + 12 - viewH));
    verticalScrollBar()->setPageStep(std::max(20, viewH));
}
void TimelineView::resizeEvent(QResizeEvent *e) { QAbstractScrollArea::resizeEvent(e); updateScrollBars(); invalidate(); }

void TimelineView::setZoom(double pps, double anchorTime) {
    pps = std::clamp(pps, 2.0, 1200.0);
    if (anchorTime < 0) anchorTime = m->playhead();
    const double anchorX = timeToX(anchorTime);
    m_pps = pps;
    updateScrollBars();
    horizontalScrollBar()->setValue(int(kHeaderW + anchorTime * m_pps - anchorX));
    invalidate();
}
void TimelineView::zoomBy(double f) { setZoom(m_pps * f, m->playhead()); }
void TimelineView::zoomFit() {
    const double dur = std::max(1.0, m->project().duration());
    setZoom((viewport()->width() - kHeaderW - 40) / dur, 0);
    horizontalScrollBar()->setValue(0);
}
void TimelineView::ensurePlayheadVisible() {
    const double x = timeToX(m->playhead());
    if (x > viewport()->width() - 30 || x < kHeaderW)
        horizontalScrollBar()->setValue(int(m->playhead() * m_pps - (viewport()->width() - kHeaderW) * 0.25));
}

QPixmap TimelineView::thumbFor(const QString &mediaId, int h) {
    const QString path = m->thumbnail(mediaId);
    if (path.isEmpty()) return {};
    const QString key = path + "@" + QString::number(h);
    auto it = m_thumbCache.find(key);
    if (it != m_thumbCache.end()) return it.value();
    QPixmap pm(path);
    if (!pm.isNull()) pm = pm.scaledToHeight(h, Qt::SmoothTransformation);
    m_thumbCache.insert(key, pm);
    return pm;
}

Clip TimelineView::effective(const Clip &c) const {
    if (m_drag == Drag::Move && m_groupOrig.contains(c.id)) {
        Clip g = c; g.start = m_groupOrig.value(c.id) + (m_ghostStart - m_origStart); return g;
    }
    if (c.id != m_dragClip || (m_drag != Drag::TrimL && m_drag != Drag::TrimR)) return c;
    Clip g = c; g.start = m_ghostStart; g.in = m_ghostIn; g.out = m_ghostOut;
    return g;
}

void TimelineView::paintWave(QPainter &p, const Clip &c, const QRect &r, const QVector<float> &peaks, const QColor &col) {
    if (peaks.isEmpty() || r.width() < 2) return;
    p.save();
    p.setClipRect(r.intersected(viewport()->rect()));
    p.setPen(col);
    const int midY = r.center().y(), half = r.height() / 2 - 1;
    const int xStart = std::max(r.left(), kHeaderW), xEnd = std::min(r.right(), viewport()->width());
    for (int x = xStart; x <= xEnd; ++x) {
        const double t0 = (x - r.left()) / m_pps, t1 = (x + 1 - r.left()) / m_pps;
        double s0 = c.in + t0 * c.fx.speed, s1 = c.in + t1 * c.fx.speed;
        if (c.fx.reverse) { const double len = c.out - c.in; s0 = c.in + len - (s0 - c.in); s1 = c.in + len - (s1 - c.in); std::swap(s0, s1); }
        int i0 = int(s0 * 100), i1 = std::max(i0 + 1, int(s1 * 100));
        if (i0 < 0 || i0 >= peaks.size()) continue;
        i1 = std::min<int>(i1, peaks.size());
        float mx = 0;
        const int stride = std::max(1, (i1 - i0) / 24);
        for (int i = i0; i < i1; i += stride) mx = std::max(mx, peaks[i]);
        mx = std::min(1.0f, mx * float(c.fx.volume));
        const int h = std::max(1, int(mx * half));
        p.drawLine(x, midY - h, x, midY + h);
    }
    p.restore();
}

void TimelineView::paintClip(QPainter &p, const Clip &cOrig, int ti, bool ghost) {
    const Clip c = ghost ? cOrig : effective(cOrig);
    const Track &tr = m->project().tracks[ti];
    const QRect r = clipRect(c, ti);
    if (r.right() < kHeaderW || r.left() > viewport()->width()) return;
    const MediaItem *media = c.isTitle() ? nullptr : m->project().mediaById(c.mediaId);
    QString kind = c.isTitle() ? "title" : (media ? media->kind : "video");
    if (isAudioType(tr.type)) kind = toString(tr.type);
    QColor base = Theme::clipColor(kind);
    const bool missing = media && media->missing;
    if (missing) base = QColor(170, 50, 50);
    const bool selected = m->isSelected(c.id) && !ghost;
    const bool dimmed = !m->project().trackVisible(tr) && !isAudioType(tr.type);
    p.save();
    p.setClipRect(QRect(kHeaderW, kRulerH, viewport()->width() - kHeaderW, viewport()->height() - kRulerH));
    QPainterPath path; path.addRoundedRect(r.adjusted(0, 0, -1, -1), 4, 4);
    p.setOpacity(ghost ? 0.55 : (dimmed ? 0.35 : 1.0));
    p.fillPath(path, base.darker(selected ? 100 : 125));
    p.setClipPath(path);
    if (media && (kind == "video" || kind == "image") && r.height() > 30) {
        QPixmap th = thumbFor(media->id, r.height() - 2);
        if (!th.isNull()) { p.setOpacity(0.85); p.drawPixmap(r.left() + 1, r.top() + 1, th); p.setOpacity(1); }
    }
    if (media && media->hasAudio && kind != "image") {
        if (auto *w = m->waveform(media->id)) {
            QRect wr = isAudioType(tr.type) ? r.adjusted(0, 14, 0, -2) : QRect(r.left(), r.bottom() - r.height() / 3, r.width(), r.height() / 3);
            paintWave(p, c, wr, *w, QColor(255, 255, 255, isAudioType(tr.type) ? 190 : 120));
        }
    }
    p.setPen(QPen(QColor(255, 255, 255, 160), 1));
    if (c.fx.fadeIn > 0) p.drawLine(r.left(), r.bottom(), r.left() + int(c.fx.fadeIn * m_pps), r.top() + 2);
    if (c.fx.fadeOut > 0) p.drawLine(r.right() - int(c.fx.fadeOut * m_pps), r.top() + 2, r.right(), r.bottom());
    if (c.fx.transition != "none") {
        const int tw = int(std::min(c.fx.transitionDur, c.duration()) * m_pps);
        p.fillRect(QRect(r.left(), r.top(), tw, r.height()), QColor(255, 255, 255, 45));
        QPolygon tri; tri << QPoint(r.left(), r.top()) << QPoint(r.left() + 12, r.top()) << QPoint(r.left(), r.top() + 12);
        p.setBrush(Qt::white); p.setPen(Qt::NoPen); p.drawPolygon(tri);
    }
    QString label = c.isTitle() ? QString::fromUtf8("T  ") + c.title.text.simplified() : (media ? media->name : QString("?"));
    if (!c.label.isEmpty()) label = c.label;
    if (std::fabs(c.fx.speed - 1) > 1e-6) label += QString("  [%1x]").arg(QString::number(c.fx.speed, 'g', 4));
    if (c.fx.reverse) label += "  [rev]";
    if (c.fx.mute) label += "  [muted]";
    if (missing) label += "  (missing)";
    QFont f = p.font(); f.setPointSizeF(8.5); p.setFont(f);
    p.setPen(Qt::white);
    const QRect tr2 = r.adjusted(18, 1, -4, 0);
    if (tr2.width() > 10) p.drawText(tr2, Qt::AlignLeft | Qt::AlignTop, QFontMetrics(f).elidedText(label, Qt::ElideRight, tr2.width()));
    p.setClipping(false);
    p.setOpacity(1);
    const bool primary = selected && c.id == m->selectedClip();
    p.setPen(QPen(selected ? (primary ? QColor(255, 220, 80) : QColor(120, 220, 255)) : QColor(0, 0, 0, 120), selected ? 2 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.restore();
}

void TimelineView::paintStatic(QPainter &p) {
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(QRect(0, 0, viewport()->width(), viewport()->height()), Theme::bg());
    const Project &pr = m->project();
    const QRect area(kHeaderW, kRulerH, viewport()->width() - kHeaderW, viewport()->height() - kRulerH);

    for (int i = 0; i < pr.tracks.size(); ++i) {
        const int top = trackTop(i);
        if (top + pr.tracks[i].height < kRulerH || top > viewport()->height()) continue;
        p.fillRect(QRect(kHeaderW, top, viewport()->width() - kHeaderW, pr.tracks[i].height), (i % 2) ? Theme::trackB() : Theme::trackA());
        p.setPen(Theme::grid()); p.drawLine(0, top + pr.tracks[i].height - 1, viewport()->width(), top + pr.tracks[i].height - 1);
    }
    // very transparent colour regions between marker lines
    if (m_regions && !pr.markers.isEmpty()) {
        QVector<double> pts;
        for (auto &mk : pr.markers) pts << mk.time;
        std::sort(pts.begin(), pts.end());
        const double endT = std::max({pr.duration(), pts.last(), xToTime(viewport()->width())}) + 1;
        double prev = 0;
        for (int i = 0; i <= pts.size(); ++i) {
            const double to = i < pts.size() ? pts[i] : endT;
            const int x0 = std::max(kHeaderW, int(timeToX(prev))), x1 = std::min(viewport()->width(), int(timeToX(to)));
            if (x1 > x0) p.fillRect(QRect(x0, kRulerH, x1 - x0, viewport()->height() - kRulerH), regionColor(pr, i));
            prev = to;
        }
    }
    static const double steps[] = {0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    double step = 3600;
    for (double s : steps) if (s * m_pps >= 80) { step = s; break; }
    const double t0 = std::max(0.0, xToTime(kHeaderW)), t1 = xToTime(viewport()->width());
    p.setPen(QColor(Theme::grid().red(), Theme::grid().green(), Theme::grid().blue(), 70));
    for (double t = std::floor(t0 / step) * step; t < t1; t += step) {
        const int x = int(timeToX(t));
        if (x >= kHeaderW) p.drawLine(x, kRulerH, x, viewport()->height());
    }
    // clips
    for (int i = 0; i < pr.tracks.size(); ++i) {
        const int top = trackTop(i);
        if (top + pr.tracks[i].height < kRulerH || top > viewport()->height()) continue;
        for (const Clip &c : pr.clips) if (c.trackId == pr.tracks[i].id) {
            if (m_drag == Drag::Move && m_groupOrig.size() == 1 && c.id == m_dragClip && m_ghostTrack != pr.tracks[i].id) continue;
            paintClip(p, c, i, false);
        }
        if (m_drag == Drag::Move && m_groupOrig.size() == 1) if (const Clip *dc = pr.clipById(m_dragClip)) if (m_ghostTrack == pr.tracks[i].id && dc->trackId != m_ghostTrack) {
            Clip g = effective(*dc); g.trackId = m_ghostTrack; paintClip(p, g, i, true);
        }
    }
    if (m_dropActive) {
        const int ti = pr.trackIndex(m_dropTrack);
        if (ti >= 0) {
            QRect r(int(timeToX(m_dropStart)), trackTop(ti) + 3, std::max(4, int(m_dropDur * m_pps)), pr.tracks[ti].height - 6);
            p.setPen(QPen(QColor(255, 220, 80), 2, Qt::DashLine)); p.setBrush(QColor(255, 220, 80, 50)); p.drawRect(r);
        }
    }
    if (m_drag == Drag::Rubber && m_bandMoved) {
        p.setClipRect(area);
        p.setPen(QPen(QColor(120, 200, 255), 1, Qt::DashLine)); p.setBrush(QColor(120, 200, 255, 40)); p.drawRect(m_band);
    }
    // headers
    p.setClipping(false);
    p.fillRect(QRect(0, 0, kHeaderW, viewport()->height()), Theme::panel());
    QFont hf = p.font(); hf.setPointSizeF(8.5); p.setFont(hf);
    bool anySolo = false; for (auto &t : pr.tracks) if (t.solo) anySolo = true;
    for (int i = 0; i < pr.tracks.size(); ++i) {
        const Track &t = pr.tracks[i];
        const int top = trackTop(i);
        if (top + t.height < kRulerH || top > viewport()->height()) continue;
        if (m_drag == Drag::TrackMove && i == m_moveFrom) p.fillRect(QRect(0, top, kHeaderW, t.height), QColor(66, 133, 244, 60));
        p.setPen(Theme::grid()); p.drawLine(0, top + t.height - 1, kHeaderW, top + t.height - 1);
        p.setPen(t.locked ? Theme::dim() : Theme::text());
        p.drawText(QRect(8, top + 4, kHeaderW - 110, 20), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(hf).elidedText(t.name, Qt::ElideRight, kHeaderW - 110));
        p.setPen(Theme::dim());
        static const char *typeLabel[] = {"video", "audio", "music", "voice", "title"};
        QString sub = typeLabel[int(t.type)];
        if (anySolo && !t.solo) sub += QString::fromUtf8("  · silent (solo)");
        else if (t.muted) sub += QString::fromUtf8("  · muted");
        else if (t.hidden) sub += QString::fromUtf8("  · hidden");
        p.drawText(QRect(8, top + 24, kHeaderW - 16, 16), Qt::AlignVCenter | Qt::AlignLeft, sub);
        struct B { const char *txt; bool on; QColor col; int idx; };
        QVector<B> btns = {{"M", t.muted, QColor(220, 90, 60), 0}, {"S", t.solo, QColor(230, 190, 40), 1}, {"L", t.locked, QColor(150, 150, 160), 2}};
        if (t.type == TrackType::Video || t.type == TrackType::Title) btns.push_back({"H", t.hidden, QColor(120, 120, 220), 3});
        for (auto &b : btns) {
            QRect br = headerButton(b.idx, i);
            p.setPen(Qt::NoPen); p.setBrush(b.on ? b.col : QColor(128, 128, 140, 70)); p.drawRoundedRect(br, 3, 3);
            p.setPen(b.on ? Qt::black : Theme::text()); p.drawText(br, Qt::AlignCenter, b.txt);
        }
        if (t.type != TrackType::Video && t.type != TrackType::Title && std::fabs(t.volume - 1) > 0.01) {
            p.setPen(Theme::dim()); p.drawText(QRect(8, top + 40, kHeaderW - 20, 14), Qt::AlignLeft, QString("vol %1%").arg(int(t.volume * 100)));
        }
    }
    if (m_drag == Drag::TrackMove && m_moveIns >= 0) {
        const int y = m_moveIns < pr.tracks.size() ? trackTop(m_moveIns) : trackTop(pr.tracks.size() - 1) + pr.tracks.last().height;
        p.setPen(QPen(QColor(255, 220, 80), 3)); p.drawLine(0, y, viewport()->width(), y);
    }
    p.setPen(Theme::grid()); p.drawLine(kHeaderW - 1, 0, kHeaderW - 1, viewport()->height());

    // ruler
    p.fillRect(QRect(0, 0, viewport()->width(), kRulerH), Theme::panel());
    p.setPen(Theme::grid()); p.drawLine(0, kRulerH - 1, viewport()->width(), kRulerH - 1);
    p.setClipRect(QRect(kHeaderW, 0, viewport()->width() - kHeaderW, kRulerH));
    const double minor = step / 5.0;
    for (double t = std::floor(t0 / minor) * minor; t < t1; t += minor) { const int x = int(timeToX(t)); p.setPen(Theme::grid()); p.drawLine(x, kRulerH - 5, x, kRulerH - 1); }
    for (double t = std::floor(t0 / step) * step; t < t1; t += step) {
        const int x = int(timeToX(t));
        p.setPen(Theme::dim()); p.drawLine(x, kRulerH - 12, x, kRulerH - 1);
        p.setPen(Theme::text()); p.drawText(x + 4, 6, 90, kRulerH - 12, Qt::AlignLeft | Qt::AlignVCenter, humanTime(t, step < 1));
    }
    for (const Marker &mk : pr.markers) {
        const int x = int(timeToX(mk.time));
        p.setPen(Qt::NoPen); p.setBrush(QColor(250, 190, 40));
        QPolygon flag; flag << QPoint(x, 3) << QPoint(x + 9, 3) << QPoint(x + 9, 10) << QPoint(x, 15);
        p.drawPolygon(flag);
        if (!mk.name.isEmpty()) { p.setPen(QColor(250, 190, 40)); p.drawText(x + 12, 3, 80, 12, Qt::AlignLeft | Qt::AlignVCenter, mk.name); }
    }
    p.setClipRect(area);
    for (const Marker &mk : pr.markers) { p.setPen(QColor(250, 190, 40, 130)); const int x = int(timeToX(mk.time)); p.drawLine(x, kRulerH, x, viewport()->height()); }
}

void TimelineView::paintPlayhead(QPainter &p) {
    const int px = int(timeToX(m->playhead()));
    if (px < kHeaderW - 1) return;
    p.save();
    p.setClipRect(QRect(kHeaderW, 0, viewport()->width() - kHeaderW, viewport()->height()));
    p.setPen(Qt::NoPen); p.setBrush(Theme::accent());
    p.drawRect(px - 1, 0, 3, viewport()->height());                       // 3 px wide line
    QPolygon h; h << QPoint(px - 10, 0) << QPoint(px + 10, 0) << QPoint(px + 10, 12) << QPoint(px, 24) << QPoint(px - 10, 12);   // wide grip
    p.drawPolygon(h);
    p.restore();
}

void TimelineView::paintEvent(QPaintEvent *e) {
    const qreal dpr = viewport()->devicePixelRatioF();
    if (m_dirty || m_cache.size() != viewport()->size() * dpr) {
        m_cache = QPixmap(viewport()->size() * dpr);
        m_cache.setDevicePixelRatio(dpr);
        QPainter cp(&m_cache);
        paintStatic(cp);
        m_dirty = false;
    }
    QPainter p(viewport());
    p.setClipRegion(e->region());
    p.drawPixmap(0, 0, m_cache);
    paintPlayhead(p);
}

QString TimelineView::headerButtonTip(const QPoint &pt) const {
    const int ti = trackAt(pt.y());
    if (ti < 0 || pt.x() >= kHeaderW) return {};
    const Track &t = m->project().tracks[ti];
    if (headerButton(0, ti).contains(pt)) return tr("Mute – silence this track in the preview and in the export.");
    if (headerButton(1, ti).contains(pt)) return tr("Solo – hear ONLY the soloed tracks; all other tracks go silent.\nOn a video or title track it also shows only the soloed picture tracks.\nClick again to turn it off.");
    if (headerButton(2, ti).contains(pt)) return tr("Lock – protects this track's clips from being moved or changed.");
    if ((t.type == TrackType::Video || t.type == TrackType::Title) && headerButton(3, ti).contains(pt)) return tr("Hide – do not show this track's picture (sound is not affected).");
    return tr("Drag this header up or down to reorder tracks. Right-click for more options.\nVideo tracks lower in the list are drawn on top of those above them; titles are always on top.");
}

bool TimelineView::viewportEvent(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const QString tip = headerButtonTip(he->pos());
        if (tip.isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), tip, viewport());
        return true;
    }
    return QAbstractScrollArea::viewportEvent(e);
}

TimelineView::Hit TimelineView::hitTest(const QPoint &pt) const {
    Hit h;
    h.track = trackAt(pt.y());
    if (h.track < 0 || pt.x() < kHeaderW) return h;
    const Project &pr = m->project();
    const Track &t = pr.tracks[h.track];
    for (const Clip &c : pr.clips) {
        if (c.trackId != t.id) continue;
        const QRect r = clipRect(c, h.track);
        if (r.contains(pt)) {
            h.clipId = c.id;
            const int edge = std::min(8, r.width() / 3);
            h.zone = pt.x() - r.left() < edge ? 1 : (r.right() - pt.x() < edge ? 2 : 0);
            return h;
        }
    }
    return h;
}

double TimelineView::snapTime(double t, double dur, const QString &ignore, bool useEnd) const {
    if (!m_snap) return std::max(0.0, t);
    const double thr = 9.0 / m_pps;
    QVector<double> pts = {0.0, m->playhead()};
    for (auto &mk : m->project().markers) pts.push_back(mk.time);
    for (auto &c : m->project().clips) if (c.id != ignore && !m_groupOrig.contains(c.id)) { pts.push_back(c.start); pts.push_back(c.end()); }
    double best = t, bestD = thr;
    for (double q : pts) {
        if (std::fabs(q - t) < bestD) { bestD = std::fabs(q - t); best = q; }
        if (useEnd && std::fabs(q - (t + dur)) < bestD) { bestD = std::fabs(q - (t + dur)); best = q - dur; }
    }
    return std::max(0.0, best);
}

void TimelineView::toggleTrackFlag(int ti, int which) {
    const QString id = m->project().tracks[ti].id;
    static const char *names[] = {"Mute track", "Solo track", "Lock track", "Hide track"};
    m->mutate(tr(names[which]), [id, which](Project &p) {
        Track *t = p.trackById(id); if (!t) return;
        if (which == 0) t->muted = !t->muted; else if (which == 1) t->solo = !t->solo; else if (which == 2) t->locked = !t->locked; else t->hidden = !t->hidden;
    });
    if (which == 1) {
        QStringList solos; for (auto &t : m->project().tracks) if (t.solo) solos << t.name;
        emit message(solos.isEmpty() ? tr("Solo is off – all tracks play normally.")
                                     : tr("Solo: only %1 will be heard (other tracks are silent). Click S again to turn it off.").arg(solos.join(", ")));
    }
}

void TimelineView::mousePressEvent(QMouseEvent *e) {
    setFocus();
    const QPoint pt = e->pos();
    m_pressPos = pt;
    const Project &pr = m->project();
    if (pt.y() < kRulerH) {
        if (pt.x() < kHeaderW) return;
        if (e->button() == Qt::RightButton) { emit rulerContextMenu(std::max(0.0, xToTime(pt.x())), e->globalPosition().toPoint()); return; }
        m_drag = Drag::Playhead; m->setPlayhead(snapTime(xToTime(pt.x()), 0, {}, false));
        return;
    }
    if (pt.x() < kHeaderW) {
        const int ti = trackAt(pt.y());
        if (ti < 0) return;
        if (e->button() == Qt::RightButton) { emit trackContextMenu(pr.tracks[ti].id, e->globalPosition().toPoint()); return; }
        const Track &t = pr.tracks[ti];
        if (headerButton(0, ti).contains(pt)) { toggleTrackFlag(ti, 0); return; }
        if (headerButton(1, ti).contains(pt)) { toggleTrackFlag(ti, 1); return; }
        if (headerButton(2, ti).contains(pt)) { toggleTrackFlag(ti, 2); return; }
        if ((t.type == TrackType::Video || t.type == TrackType::Title) && headerButton(3, ti).contains(pt)) { toggleTrackFlag(ti, 3); return; }
        if (pt.y() > trackTop(ti) + t.height - 6) { m_drag = Drag::TrackHeight; m_resizeTrack = ti; m_resizeOrigH = t.height; return; }
        m_drag = Drag::TrackMove; m_moveFrom = ti; m_moveIns = -1;
        return;
    }
    const Hit h = hitTest(pt);
    const bool ctrl = e->modifiers() & Qt::ControlModifier, shift = e->modifiers() & Qt::ShiftModifier;
    if (h.clipId.isEmpty()) {
        if (e->button() == Qt::RightButton && h.track >= 0) { emit laneContextMenu(pr.tracks[h.track].id, std::max(0.0, xToTime(pt.x())), e->globalPosition().toPoint()); return; }
        if (e->button() != Qt::LeftButton) return;
        m_drag = Drag::Rubber; m_bandMoved = false; m_band = QRect(pt, pt);
        m_bandBase = (ctrl || shift) ? m->selectedClips() : QStringList();
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (!m->isSelected(h.clipId)) m->selectClip(h.clipId); else m->makePrimary(h.clipId);
        emit clipContextMenu(h.clipId, e->globalPosition().toPoint());
        return;
    }
    if (ctrl) { m->toggleClip(h.clipId); return; }
    if (shift) { m->addToSelection(h.clipId); return; }
    if (!m->isSelected(h.clipId)) m->selectClip(h.clipId); else m->makePrimary(h.clipId);
    const Clip *c = pr.clipById(h.clipId);
    if (!c) return;
    for (const QString &id : m->selectedClips())
        if (const Clip *sc = pr.clipById(id)) if (pr.trackById(sc->trackId)->locked) { emit message(tr("A selected clip is on a locked track.")); return; }
    m_dragClip = c->id; m_origStart = m_ghostStart = c->start; m_origIn = m_ghostIn = c->in; m_origOut = m_ghostOut = c->out;
    m_ghostTrack = c->trackId;
    m_groupOrig.clear();
    const bool single = m->selectedClips().size() <= 1;
    m_drag = single && h.zone == 1 ? Drag::TrimL : single && h.zone == 2 ? Drag::TrimR : Drag::Move;
    if (m_drag == Drag::Move) for (const QString &id : m->selectedClips()) if (const Clip *sc = pr.clipById(id)) m_groupOrig.insert(id, sc->start);
}

void TimelineView::mouseMoveEvent(QMouseEvent *e) {
    const QPoint pt = e->pos();
    const Project &pr = m->project();
    if (m_drag == Drag::None) {
        const Hit h = hitTest(pt);
        if (pt.x() < kHeaderW && pt.y() >= kRulerH) {
            const int ti = trackAt(pt.y());
            viewport()->setCursor(ti >= 0 && pt.y() > trackTop(ti) + pr.tracks[ti].height - 6 ? Qt::SplitVCursor : Qt::ArrowCursor);
        } else viewport()->setCursor(h.zone && m->selectedClips().size() <= 1 ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    if (m_drag == Drag::Playhead) { m->setPlayhead(snapTime(xToTime(std::max(pt.x(), kHeaderW)), 0, {}, false)); return; }
    if (m_drag == Drag::TrackHeight) {
        const int h = std::clamp(m_resizeOrigH + pt.y() - m_pressPos.y(), 36, 220);
        const QString id = pr.tracks[m_resizeTrack].id;
        m->mutate("Resize track", [id, h](Project &p) { if (Track *t = p.trackById(id)) t->height = h; }, "trackheight:" + id);
        return;
    }
    if (m_drag == Drag::TrackMove) {
        if (std::abs(pt.y() - m_pressPos.y()) < 6) return;
        int ins = pr.tracks.size();
        for (int i = 0; i < pr.tracks.size(); ++i) if (pt.y() < trackTop(i) + pr.tracks[i].height / 2) { ins = i; break; }
        m_moveIns = ins; viewport()->setCursor(Qt::ClosedHandCursor); invalidate();
        return;
    }
    if (m_drag == Drag::Rubber) {
        if (!m_bandMoved && (pt - m_pressPos).manhattanLength() < 5) return;
        m_bandMoved = true;
        m_band = QRect(m_pressPos, pt).normalized();
        QStringList ids = m_bandBase;
        for (int i = 0; i < pr.tracks.size(); ++i)
            for (const Clip &c : pr.clips) if (c.trackId == pr.tracks[i].id && clipRect(c, i).intersects(m_band) && !ids.contains(c.id)) ids << c.id;
        m->setSelection(ids);
        invalidate();
        return;
    }
    const Clip *c = pr.clipById(m_dragClip);
    if (!c) return;
    const double dt = (pt.x() - m_pressPos.x()) / m_pps;
    const double speed = std::max(c->fx.speed, 0.01);
    const MediaItem *media = c->isTitle() ? nullptr : pr.mediaById(c->mediaId);
    if (m_drag == Drag::Move) {
        double minStart = 1e12; for (auto it = m_groupOrig.begin(); it != m_groupOrig.end(); ++it) minStart = std::min(minStart, it.value());
        double ns = snapTime(m_origStart + dt, c->duration(), c->id);
        ns = std::max(ns, m_origStart - minStart);                         // the whole group must stay at time >= 0
        m_ghostStart = ns;
        m_ghostTrack = c->trackId;
        if (m_groupOrig.size() == 1) {
            const int ti = trackAt(pt.y());
            if (ti >= 0 && compatible(*c, media, pr.tracks[ti]) && !pr.tracks[ti].locked) m_ghostTrack = pr.tracks[ti].id;
        }
    } else if (m_drag == Drag::TrimL) {
        double newStart = snapTime(m_origStart + dt, 0, c->id, false);
        double delta = newStart - m_origStart;
        double newIn = m_origIn + delta * speed;
        if (newIn < 0) { delta = -m_origIn / speed; newIn = 0; }
        if ((m_origOut - newIn) / speed < 0.05) { delta = (m_origOut - m_origIn) / speed - 0.05; newIn = m_origIn + delta * speed; }
        double prevEnd = 0;
        for (auto *o : pr.clipsOnTrack(c->trackId)) if (o->id != c->id && o->end() <= m_origStart + 1e-6) prevEnd = std::max(prevEnd, o->end());
        if (m_origStart + delta < prevEnd) { delta = prevEnd - m_origStart; newIn = m_origIn + delta * speed; }
        m_ghostStart = m_origStart + delta; m_ghostIn = newIn; m_ghostOut = m_origOut;
    } else if (m_drag == Drag::TrimR) {
        const double origEnd = m_origStart + (m_origOut - m_origIn) / speed;
        double newEnd = snapTime(origEnd + dt, 0, c->id, false);
        double newOut = m_origIn + (newEnd - m_origStart) * speed;
        if (media && media->kind != "image") newOut = std::min(newOut, media->duration);
        newOut = std::max(newOut, m_origIn + 0.05 * speed);
        double nextStart = 1e12;
        for (auto *o : pr.clipsOnTrack(c->trackId)) if (o->id != c->id && o->start >= origEnd - 1e-6) nextStart = std::min(nextStart, o->start);
        if (m_origStart + (newOut - m_origIn) / speed > nextStart) newOut = m_origIn + (nextStart - m_origStart) * speed;
        m_ghostStart = m_origStart; m_ghostIn = m_origIn; m_ghostOut = newOut;
    }
    invalidate();
}

void TimelineView::mouseReleaseEvent(QMouseEvent *e) {
    const Drag d = m_drag;
    m_drag = Drag::None;
    const Project &pr = m->project();
    if (d == Drag::TrackMove) {
        if (m_moveIns >= 0 && m_moveFrom >= 0 && m_moveIns != m_moveFrom && m_moveIns != m_moveFrom + 1) {
            const int from = m_moveFrom, to = m_moveIns > m_moveFrom ? m_moveIns - 1 : m_moveIns;
            m->mutate(tr("Move track"), [from, to](Project &p) { if (from < p.tracks.size()) { Track t = p.tracks.takeAt(from); p.tracks.insert(std::min<int>(to, p.tracks.size()), t); } });
            emit message(tr("Track moved."));
        }
        m_moveFrom = m_moveIns = -1; invalidate();
        return;
    }
    if (d == Drag::Rubber) {
        if (!m_bandMoved && e->button() == Qt::LeftButton) {
            const bool keep = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
            if (!keep) m->selectClip({});
            m->setPlayhead(snapTime(xToTime(e->pos().x()), 0, {}, false));
        }
        m_bandMoved = false; invalidate();
        return;
    }
    if (d == Drag::Move) {
        const double delta = m_ghostStart - m_origStart;
        const QString id = m_dragClip, gt = m_ghostTrack;
        const QHash<QString, double> orig = m_groupOrig;
        m_groupOrig.clear(); m_dragClip.clear();
        if (std::fabs(delta) < 1e-6 && (!pr.clipById(id) || pr.clipById(id)->trackId == gt)) { invalidate(); return; }
        if (orig.size() <= 1) {
            m->mutate(tr("Move clip"), [=](Project &p) {
                Clip *c = p.clipById(id); if (!c) return;
                c->trackId = gt;
                c->start = p.resolveOverlap(gt, orig.value(id) + delta, c->duration(), id);
            });
        } else {
            // validate: moved clips must not collide with clips that are not part of the move
            bool ok = true;
            for (auto it = orig.begin(); it != orig.end() && ok; ++it) {
                const Clip *c = pr.clipById(it.key()); if (!c) continue;
                const double s = it.value() + delta, en = s + c->duration();
                for (const Clip &o : pr.clips) if (o.trackId == c->trackId && !orig.contains(o.id) && s < o.end() - 1e-6 && en > o.start + 1e-6) ok = false;
            }
            if (!ok) emit message(tr("There is not enough room there for all selected clips."));
            else m->mutate(tr("Move clips"), [=](Project &p) { for (auto it = orig.begin(); it != orig.end(); ++it) if (Clip *c = p.clipById(it.key())) c->start = it.value() + delta; });
        }
        invalidate();
        return;
    }
    if (d == Drag::TrimL || d == Drag::TrimR) {
        const QString id = m_dragClip;
        const double gs = m_ghostStart, gi = m_ghostIn, go = m_ghostOut;
        const bool changed = std::fabs(gs - m_origStart) > 1e-6 || std::fabs(gi - m_origIn) > 1e-6 || std::fabs(go - m_origOut) > 1e-6;
        m_dragClip.clear();
        if (changed) m->mutate(tr("Trim clip"), [=](Project &p) { if (Clip *c = p.clipById(id)) { c->in = gi; c->out = go; c->start = gs; } });
    }
    m_dragClip.clear();
    invalidate();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent *e) {
    const Hit h = hitTest(e->pos());
    if (!h.clipId.isEmpty()) if (const Clip *c = m->project().clipById(h.clipId)) m->setPlayhead(c->start);
}

void TimelineView::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        const double anchor = xToTime(e->position().x());
        setZoom(m_pps * (e->angleDelta().y() > 0 ? 1.2 : 1 / 1.2), anchor);
        e->accept();
    } else if (e->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->angleDelta().y());
        e->accept();
    } else QAbstractScrollArea::wheelEvent(e);
}

void TimelineView::dragEnterEvent(QDragEnterEvent *e) { if (e->mimeData()->hasFormat(kMediaMime)) e->acceptProposedAction(); }
void TimelineView::dragLeaveEvent(QDragLeaveEvent *) { m_dropActive = false; invalidate(); }
void TimelineView::dragMoveEvent(QDragMoveEvent *e) {
    if (!e->mimeData()->hasFormat(kMediaMime)) return;
    const Project &pr = m->project();
    const MediaItem *media = pr.mediaById(QString::fromUtf8(e->mimeData()->data(kMediaMime)));
    const int ti = trackAt(e->position().toPoint().y());
    if (!media || ti < 0) { m_dropActive = false; e->ignore(); invalidate(); return; }
    const Track &t = pr.tracks[ti];
    Clip probe; probe.mediaId = media->id;
    if (!compatible(probe, media, t) || t.locked) { m_dropActive = false; e->ignore(); invalidate(); return; }
    m_dropDur = media->duration;
    m_dropStart = snapTime(xToTime(e->position().x()), m_dropDur, {});
    m_dropTrack = t.id; m_dropActive = true;
    e->acceptProposedAction();
    invalidate();
}
void TimelineView::dropEvent(QDropEvent *e) {
    if (!m_dropActive) return;
    m_dropActive = false;
    const QString id = QString::fromUtf8(e->mimeData()->data(kMediaMime));
    e->acceptProposedAction();
    emit mediaDropped(id, m_dropTrack, m_dropStart);
    invalidate();
}

} // namespace mv
