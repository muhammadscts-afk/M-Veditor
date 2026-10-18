// Multitrack timeline: custom painted (cached), snapping, multi-select, drag/trim, track reordering, waveforms.
#pragma once
#include "ProjectModel.h"
#include <QAbstractScrollArea>
#include <QHash>
#include <QPixmap>
#include <QToolButton>

namespace mv {

class TimelineView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit TimelineView(ProjectModel *model, QWidget *parent = nullptr);
    double zoom() const { return m_pps; }
    void setZoom(double pxPerSec, double anchorTime = -1);
    void zoomBy(double factor);
    void zoomFit();
    void setSnapping(bool on) { m_snap = on; }
    bool snapping() const { return m_snap; }
    void setRegionsVisible(bool on) { m_regions = on; invalidate(); }
    bool regionsVisible() const { return m_regions; }
    static bool compatible(const Clip &c, const MediaItem *m, const Track &t);
    static QColor pairColor(const Project &p, const QString &trackId, int pairIndex);   // very transparent tint between a Ma/Mp pair

signals:
    void mediaDropped(const QString &mediaId, const QString &trackId, double start);
    void clipContextMenu(const QString &clipId, const QPoint &globalPos);
    void trackContextMenu(const QString &trackId, const QPoint &globalPos);
    void rulerContextMenu(double time, const QPoint &globalPos);
    void addTrackRequested(int trackType);
    void markerContextMenu(int markerIndex, const QPoint &globalPos);   // right-click on a marker head
    void laneContextMenu(const QString &trackId, double time, const QPoint &globalPos);   // right-click on an empty part of a track
    void message(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;
    bool viewportEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    enum class Drag { None, Playhead, Move, TrimL, TrimR, TrackHeight, TrackMove, Rubber };
    struct Hit { QString clipId; int zone = 0; int track = -1; };   // zone: 0 body, 1 left edge, 2 right edge

    const int kHeaderW = 170, kRulerH = 30;
    double timeToX(double t) const;
    double xToTime(double x) const;
    int trackTop(int i) const;
    int trackAt(int y) const;
    int trackIndexOf(const QString &trackId) const;
    QRect clipRect(const Clip &c, int trackIdx) const;
    QRect headerButton(int idx, int trackIdx) const;
    QString headerButtonTip(const QPoint &p) const;
    Hit hitTest(const QPoint &p) const;
    int markerAt(const QPoint &p) const;
    Clip effective(const Clip &c) const;
    double snapTime(double t, double dur, const QString &ignoreClip, bool useEnd = true) const;
    void updateScrollBars();
    void ensurePlayheadVisible();
    void invalidate();
    void paintStatic(QPainter &p);
    void paintPlayhead(QPainter &p);
    QPixmap thumbFor(const QString &mediaId, int h);
    void paintClip(QPainter &p, const Clip &c, int trackIdx, bool ghost);
    void paintWave(QPainter &p, const Clip &c, const QRect &r, const QVector<float> &peaks, const QColor &col);
    void toggleTrackFlag(int trackIdx, int which, bool additive = false);

    ProjectModel *m;
    QToolButton *m_addBtn;
    double m_pps = 80;
    bool m_snap = true, m_regions = true;
    QPixmap m_cache;
    bool m_dirty = true;
    int m_lastPhX = -100;
    Drag m_drag = Drag::None;
    QString m_dragClip, m_ghostTrack;
    QHash<QString, double> m_groupOrig;                  // start of every selected clip when a move began
    QPoint m_pressPos;
    double m_origStart = 0, m_origIn = 0, m_origOut = 0, m_ghostStart = 0, m_ghostIn = 0, m_ghostOut = 0;
    int m_resizeTrack = -1, m_resizeOrigH = 0;
    int m_moveFrom = -1, m_moveIns = -1;
    QRect m_band; bool m_bandMoved = false; QStringList m_bandBase;
    bool m_dropActive = false;
    double m_dropStart = 0, m_dropDur = 0;
    QString m_dropTrack;
    QHash<QString, QPixmap> m_thumbCache;
};

} // namespace mv
