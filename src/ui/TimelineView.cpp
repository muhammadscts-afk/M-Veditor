#include <algorithm>
#include <functional>
#include "TimelineView.h"
#include "MediaProbe.h"
#include "Theme.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <cmath>

namespace mv {

static const char *kMediaMime = "application/x-mveditor-media";

TimelineView::TimelineView(ProjectModel *model, QWidget *parent) : QAbstractScrollArea(parent), m(model) {
    setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(230);
    connect(m, &ProjectModel::changed, this, [this] { updateScrollBars(); viewport()->update(); });
    connect(m, &ProjectModel::selectionChanged, viewport(), qOverload<>(&QWidget::update));
    connect(m, &ProjectModel::assetsChanged, viewport(), qOverload<>(&QWidget::update));
    connect(m, &ProjectModel::playheadChanged, this, [this](double, bool fromPlayback) {
        if (fromPlayback) ensurePlayheadVisible();
        viewport()->update();
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    updateScrollBars();
}

bool TimelineView::compatible(const Clip &c, const MediaItem *media, const Track &t) {
    if (c.isTitle()) return t.type == TrackType::Title;
    if (!media) return false;
    if (media->kind == "audio") return isAudioType(t.type);
    return t.type == TrackType::Video;     // video and image clips
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
QRect TimelineView::clipRect(const Clip &c, int trackIdx) const {
    const int top = trackTop(trackIdx);
    const int x0 = int(timeToX(c.start)), x1 = int(timeToX(c.end()));
    return QRect(x0, top + 3, std::max(3, x1 - x0), m->project().tracks[trackIdx].height - 6);
}
QRect TimelineView::headerButton(int idx, int trackIdx) const {
    return QRect(kHeaderW - 8 - (idx + 1) * 22, trackTop(trackIdx) + 5, 20, 18);
}

void TimelineView::updateScrollBars() {
    const double dur = std::max(m->project().duration(), 10.0) + 30.0;
    const int contentW = int(dur * m_pps);
    const int viewW = viewport()->width() - kHeaderW;
    horizontalScrollBar()->setRange(0, std::max(0, contentW - viewW));
    horizontalScrollBar()->setPageStep(std::max(50, viewW));
    int total = 0;
    for (auto &t : m->project().tracks) total += t.height;
    const int viewH = viewport()->height() - kRulerH;
    verticalScrollBar()->setRange(0, std::max(0, total + 12 - viewH));
    verticalScrollBar()->setPageStep(std::max(20, viewH));
}
void TimelineView::resizeEvent(QResizeEvent *e) { QAbstractScrollArea::resizeEvent(e); updateScrollBars(); }

void TimelineView::setZoom(double pps, double anchorTime) {
    pps = std::clamp(pps, 2.0, 1200.0);
    if (anchorTime < 0) anchorTime = m->playhead();
    const double anchorX = timeToX(anchorTime);
    m_pps = pps;
    updateScrollBars();
    horizontalScrollBar()->setValue(int(kHeaderW + anchorTime * m_pps - anchorX));
    viewport()->update();
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
    if (c.id != m_dragClip || m_drag == Drag::None || m_drag == Drag::Playhead) return c;
    Clip g = c;
    g.start = m_ghostStart; g.in = m_ghostIn; g.out = m_ghostOut;
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
        const double t0 = (x - r.left()) / m_pps, t1 = (x + 1 - r.left()) / m_pps;       // timeline seconds into the clip
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
    const bool selected = (c.id == m->selectedClip()) && !ghost;
    p.save();
    p.setClipRect(QRect(kHeaderW, kRulerH, viewport()->width() - kHeaderW, viewport()->height() - kRulerH));
    QPainterPath path; path.addRoundedRect(r.adjusted(0, 0, -1, -1), 4, 4);
    p.setOpacity(ghost ? 0.55 : (tr.hidden ? 0.35 : 1.0));
    p.fillPath(path, base.darker(selected ? 100 : 125));
    p.setClipPath(path);
    // thumbnail (video / image)
    if (media && (kind == "video" || kind == "image") && r.height() > 30) {
        QPixmap th = thumbFor(media->id, r.height() - 2);
        if (!th.isNull()) { p.setOpacity(0.85); p.drawPixmap(r.left() + 1, r.top() + 1, th); p.setOpacity(1); }
    }
    // waveform
    if (media && media->hasAudio && kind != "image") {
        if (auto *w = m->waveform(media->id)) {
            QRect wr = isAudioType(tr.type) ? r.adjusted(0, 14, 0, -2) : QRect(r.left(), r.bottom() - r.height() / 3, r.width(), r.height() / 3);
            paintWave(p, c, wr, *w, QColor(255, 255, 255, isAudioType(tr.type) ? 190 : 120));
        }
    }
    // fades
    p.setPen(QPen(QColor(255, 255, 255, 160), 1));
    if (c.fx.fadeIn > 0) p.drawLine(r.left(), r.bottom(), r.left() + int(c.fx.fadeIn * m_pps), r.top() + 2);
    if (c.fx.fadeOut > 0) p.drawLine(r.right() - int(c.fx.fadeOut * m_pps), r.top() + 2, r.right(), r.bottom());
    // transition marker
    if (c.fx.transition != "none") {
        const int tw = int(std::min(c.fx.transitionDur, c.duration()) * m_pps);
        p.fillRect(QRect(r.left(), r.top(), tw, r.height()), QColor(255, 255, 255, 45));
        QPolygon tri; tri << QPoint(r.left(), r.top()) << QPoint(r.left() + 12, r.top()) << QPoint(r.left(), r.top() + 12);
        p.setBrush(Qt::white); p.setPen(Qt::NoPen); p.drawPolygon(tri);
    }
    // label
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
    p.setPen(QPen(selected ? QColor(255, 220, 80) : QColor(0, 0, 0, 120), selected ? 2 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.restore();
}

void TimelineView::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(viewport()->rect(), Theme::bg());
    const Project &pr = m->project();
    const QRect area(kHeaderW, kRulerH, viewport()->width() - kHeaderW, viewport()->height() - kRulerH);

    // track lanes
    for (int i = 0; i < pr.tracks.size(); ++i) {
        const int top = trackTop(i);
        if (top + pr.tracks[i].height < kRulerH || top > viewport()->height()) continue;
        p.fillRect(QRect(kHeaderW, top, viewport()->width() - kHeaderW, pr.tracks[i].height), (i % 2) ? Theme::trackB() : Theme::trackA());
        p.setPen(Theme::grid()); p.drawLine(0, top + pr.tracks[i].height - 1, viewport()->width(), top + pr.tracks[i].height - 1);
    }
    // grid lines
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
            // while moving, the dragged clip is drawn on its ghost track instead
            if (m_drag == Drag::Move && c.id == m_dragClip && m_ghostTrack != pr.tracks[i].id) continue;
            paintClip(p, c, i, false);
        }
        if (m_drag == Drag::Move) if (const Clip *dc = pr.clipById(m_dragClip)) if (m_ghostTrack == pr.tracks[i].id && dc->trackId != m_ghostTrack) {
            Clip g = effective(*dc); g.trackId = m_ghostTrack; paintClip(p, g, i, true);
        }
    }
    // drop ghost
    if (m_dropActive) {
        const int ti = pr.trackIndex(m_dropTrack);
        if (ti >= 0) {
            QRect r(int(timeToX(m_dropStart)), trackTop(ti) + 3, std::max(4, int(m_dropDur * m_pps)), pr.tracks[ti].height - 6);
            p.setPen(QPen(QColor(255, 220, 80), 2, Qt::DashLine)); p.setBrush(QColor(255, 220, 80, 50)); p.drawRect(r);
        }
    }
    // headers
    p.setClipping(false);
    p.fillRect(QRect(0, 0, kHeaderW, viewport()->height()), Theme::panel());
    QFont hf = p.font(); hf.setPointSizeF(8.5); p.setFont(hf);
    for (int i = 0; i < pr.tracks.size(); ++i) {
        const Track &t = pr.tracks[i];
        const int top = trackTop(i);
        if (top + t.height < kRulerH || top > viewport()->height()) continue;
        p.setPen(Theme::grid()); p.drawLine(0, top + t.height - 1, kHeaderW, top + t.height - 1);
        p.setPen(t.locked ? Theme::dim() : Theme::text());
        p.drawText(QRect(8, top + 4, kHeaderW - 100, 20), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(hf).elidedText(t.name, Qt::ElideRight, kHeaderW - 100));
        p.setPen(Theme::dim());
        static const char *typeLabel[] = {"video", "audio", "music", "voice", "title"};
        p.drawText(QRect(8, top + 22, kHeaderW - 20, 16), Qt::AlignVCenter | Qt::AlignLeft, typeLabel[int(t.type)]);
        struct B { const char *txt; bool on; QColor col; int idx; };
        QVector<B> btns = {{"M", t.muted, QColor(220, 90, 60), 0}, {"S", t.solo, QColor(230, 190, 40), 1}, {"L", t.locked, QColor(150, 150, 160), 2}};
        if (t.type == TrackType::Video || t.type == TrackType::Title) btns.push_back({"H", t.hidden, QColor(120, 120, 220), 3});
        for (auto &b : btns) {
            QRect br = headerButton(b.idx, i);
            p.setPen(Qt::NoPen); p.setBrush(b.on ? b.col : QColor(128, 128, 140, 70)); p.drawRoundedRect(br, 3, 3);
            p.setPen(b.on ? Qt::black : Theme::text()); p.drawText(br, Qt::AlignCenter, b.txt);
        }
        if (isAudioType(t.type) || t.type == TrackType::Video) {   // tiny volume read-out
            if (std::fabs(t.volume - 1) > 0.01 && t.type != TrackType::Video) { p.setPen(Theme::dim()); p.drawText(QRect(8, top + 38, kHeaderW - 20, 14), Qt::AlignLeft, QString("vol %1%").arg(int(t.volume * 100))); }
        }
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
        p.setPen(Theme::text()); p.drawText(x + 4, 4, 90, kRulerH - 10, Qt::AlignLeft | Qt::AlignVCenter, humanTime(t, step < 1));
    }
    for (const Marker &mk : pr.markers) {
        const int x = int(timeToX(mk.time));
        p.setPen(Qt::NoPen); p.setBrush(QColor(250, 190, 40));
        QPolygon flag; flag << QPoint(x, 2) << QPoint(x + 9, 2) << QPoint(x + 9, 9) << QPoint(x, 14);
        p.drawPolygon(flag);
        if (!mk.name.isEmpty()) { p.setPen(QColor(250, 190, 40)); p.drawText(x + 12, 2, 80, 12, Qt::AlignLeft | Qt::AlignVCenter, mk.name); }
    }
    p.setClipping(false);
    // marker lines over lanes
    p.setClipRect(area);
    for (const Marker &mk : pr.markers) { p.setPen(QColor(250, 190, 40, 110)); const int x = int(timeToX(mk.time)); p.drawLine(x, kRulerH, x, viewport()->height()); }
    // playhead
    const int px = int(timeToX(m->playhead()));
    if (px >= kHeaderW - 1) {
        p.setClipRect(QRect(kHeaderW, 0, viewport()->width() - kHeaderW, viewport()->height()));
        p.setPen(QPen(Theme::accent(), 1)); p.drawLine(px, 0, px, viewport()->height());
        p.setBrush(Theme::accent()); p.setPen(Qt::NoPen);
        QPolygon h; h << QPoint(px - 6, 0) << QPoint(px + 6, 0) << QPoint(px + 6, 10) << QPoint(px, 18) << QPoint(px - 6, 10);
        p.drawPolygon(h);
    }
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
    for (auto &c : m->project().clips) if (c.id != ignore) { pts.push_back(c.start); pts.push_back(c.end()); }
    double best = t, bestD = thr;
    for (double q : pts) {
        if (std::fabs(q - t) < bestD) { bestD = std::fabs(q - t); best = q; }
        if (useEnd && std::fabs(q - (t + dur)) < bestD) { bestD = std::fabs(q - (t + dur)); best = q - dur; }
    }
    return std::max(0.0, best);
}

void TimelineView::mousePressEvent(QMouseEvent *e) {
    setFocus();
    const QPoint pt = e->pos();
    m_pressPos = pt;
    const Project &pr = m->project();
    if (pt.y() < kRulerH) {
        if (e->button() == Qt::RightButton && pt.x() >= kHeaderW) { emit rulerContextMenu(std::max(0.0, xToTime(pt.x())), e->globalPosition().toPoint()); return; }
        if (pt.x() >= kHeaderW) { m_drag = Drag::Playhead; m->setPlayhead(snapTime(xToTime(pt.x()), 0, {}, false)); }
        return;
    }
    if (pt.x() < kHeaderW) {
        const int ti = trackAt(pt.y());
        if (ti < 0) return;
        if (e->button() == Qt::RightButton) { emit trackContextMenu(pr.tracks[ti].id, e->globalPosition().toPoint()); return; }
        const Track &t = pr.tracks[ti];
        const QString id = t.id;
        auto toggle = [&](const QString &what, std::function<void(Track &)> fn) { m->mutate(what, [id, fn](Project &p) { if (Track *tt = p.trackById(id)) fn(*tt); }); };
        if (headerButton(0, ti).contains(pt)) { toggle("Mute track", [](Track &x) { x.muted = !x.muted; }); return; }
        if (headerButton(1, ti).contains(pt)) { toggle("Solo track", [](Track &x) { x.solo = !x.solo; }); return; }
        if (headerButton(2, ti).contains(pt)) { toggle("Lock track", [](Track &x) { x.locked = !x.locked; }); return; }
        if ((t.type == TrackType::Video || t.type == TrackType::Title) && headerButton(3, ti).contains(pt)) { toggle("Hide track", [](Track &x) { x.hidden = !x.hidden; }); return; }
        if (pt.y() > trackTop(ti) + t.height - 6) { m_drag = Drag::TrackHeight; m_resizeTrack = ti; m_resizeOrigH = t.height; }
        return;
    }
    const Hit h = hitTest(pt);
    if (h.clipId.isEmpty()) {
        m->selectClip({});
        if (e->button() == Qt::LeftButton) { m_drag = Drag::Playhead; m->setPlayhead(snapTime(xToTime(pt.x()), 0, {}, false)); }
        return;
    }
    m->selectClip(h.clipId);
    if (e->button() == Qt::RightButton) { emit clipContextMenu(h.clipId, e->globalPosition().toPoint()); return; }
    const Clip *c = pr.clipById(h.clipId);
    if (!c) return;
    if (pr.trackById(c->trackId)->locked) { emit message(tr("This track is locked.")); return; }
    m_dragClip = c->id; m_origStart = m_ghostStart = c->start; m_origIn = m_ghostIn = c->in; m_origOut = m_ghostOut = c->out;
    m_ghostTrack = c->trackId;
    m_drag = h.zone == 1 ? Drag::TrimL : h.zone == 2 ? Drag::TrimR : Drag::Move;
}

void TimelineView::mouseMoveEvent(QMouseEvent *e) {
    const QPoint pt = e->pos();
    const Project &pr = m->project();
    if (m_drag == Drag::None) {
        const Hit h = hitTest(pt);
        if (pt.x() < kHeaderW && pt.y() >= kRulerH) {
            const int ti = trackAt(pt.y());
            viewport()->setCursor(ti >= 0 && pt.y() > trackTop(ti) + pr.tracks[ti].height - 6 ? Qt::SplitVCursor : Qt::ArrowCursor);
        } else viewport()->setCursor(h.zone ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    if (m_drag == Drag::Playhead) { m->setPlayhead(snapTime(xToTime(std::max(pt.x(), kHeaderW)), 0, {}, false)); return; }
    if (m_drag == Drag::TrackHeight) {
        const int h = std::clamp(m_resizeOrigH + pt.y() - m_pressPos.y(), 36, 220);
        const QString id = pr.tracks[m_resizeTrack].id;
        m->mutate("Resize track", [id, h](Project &p) { if (Track *t = p.trackById(id)) t->height = h; }, "trackheight:" + id);
        return;
    }
    const Clip *c = pr.clipById(m_dragClip);
    if (!c) return;
    const double dt = (pt.x() - m_pressPos.x()) / m_pps;
    const double speed = std::max(c->fx.speed, 0.01);
    const MediaItem *media = c->isTitle() ? nullptr : pr.mediaById(c->mediaId);
    if (m_drag == Drag::Move) {
        double ns = snapTime(m_origStart + dt, c->duration(), c->id);
        m_ghostStart = ns;
        const int ti = trackAt(pt.y());
        m_ghostTrack = c->trackId;
        if (ti >= 0 && compatible(*c, media, pr.tracks[ti]) && !pr.tracks[ti].locked) m_ghostTrack = pr.tracks[ti].id;
    } else if (m_drag == Drag::TrimL) {
        double newStart = snapTime(m_origStart + dt, 0, c->id, false);
        double delta = newStart - m_origStart;
        double newIn = m_origIn + delta * speed;
        if (newIn < 0) { delta = -m_origIn / speed; newIn = 0; }
        if ((m_origOut - newIn) / speed < 0.05) { delta = (m_origOut - m_origIn) / speed - 0.05; newIn = m_origIn + delta * speed; }
        // don't run into the previous clip
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
    viewport()->update();
}

void TimelineView::mouseReleaseEvent(QMouseEvent *) {
    const Drag d = m_drag;
    m_drag = Drag::None;
    if (d == Drag::Move || d == Drag::TrimL || d == Drag::TrimR) {
        const QString id = m_dragClip;
        const double gs = m_ghostStart, gi = m_ghostIn, go = m_ghostOut;
        const QString gt = m_ghostTrack;
        const bool changed = std::fabs(gs - m_origStart) > 1e-6 || std::fabs(gi - m_origIn) > 1e-6 || std::fabs(go - m_origOut) > 1e-6 ||
                             (m->project().clipById(id) && m->project().clipById(id)->trackId != gt);
        if (changed) {
            m->mutate(d == Drag::Move ? "Move clip" : "Trim clip", [=](Project &p) {
                Clip *c = p.clipById(id);
                if (!c) return;
                c->in = gi; c->out = go;
                c->trackId = gt;
                c->start = d == Drag::Move ? p.resolveOverlap(gt, gs, c->duration(), id) : gs;
            });
        }
    }
    m_dragClip.clear();
    viewport()->update();
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
void TimelineView::dragLeaveEvent(QDragLeaveEvent *) { m_dropActive = false; viewport()->update(); }
void TimelineView::dragMoveEvent(QDragMoveEvent *e) {
    if (!e->mimeData()->hasFormat(kMediaMime)) return;
    const Project &pr = m->project();
    const MediaItem *media = pr.mediaById(QString::fromUtf8(e->mimeData()->data(kMediaMime)));
    const int ti = trackAt(e->position().toPoint().y());
    if (!media || ti < 0) { m_dropActive = false; e->ignore(); viewport()->update(); return; }
    const Track &t = pr.tracks[ti];
    const bool ok = media->kind == "audio" ? isAudioType(t.type) : (t.type == TrackType::Video || (isAudioType(t.type) && media->hasAudio));
    if (!ok || t.locked) { m_dropActive = false; e->ignore(); viewport()->update(); return; }
    m_dropDur = media->duration;
    m_dropStart = snapTime(xToTime(e->position().x()), m_dropDur, {});
    m_dropTrack = t.id; m_dropActive = true;
    e->acceptProposedAction();
    viewport()->update();
}
void TimelineView::dropEvent(QDropEvent *e) {
    if (!m_dropActive) return;
    m_dropActive = false;
    const QString id = QString::fromUtf8(e->mimeData()->data(kMediaMime));
    e->acceptProposedAction();
    emit mediaDropped(id, m_dropTrack, m_dropStart);
    viewport()->update();
}

} // namespace mv
