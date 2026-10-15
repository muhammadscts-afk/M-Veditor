// Multitrack timeline: custom painted, with snapping, drag/trim editing, waveforms and track controls.
#pragma once
#include "ProjectModel.h"
#include <QAbstractScrollArea>
#include <QHash>
#include <QPixmap>

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
    static bool compatible(const Clip &c, const MediaItem *m, const Track &t);

signals:
    void mediaDropped(const QString &mediaId, const QString &trackId, double start);
    void clipContextMenu(const QString &clipId, const QPoint &globalPos);
    void trackContextMenu(const QString &trackId, const QPoint &globalPos);
    void rulerContextMenu(double time, const QPoint &globalPos);
    void message(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;
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
    enum class Drag { None, Playhead, Move, TrimL, TrimR, TrackHeight };
    struct Hit { QString clipId; int zone = 0; int track = -1; };   // zone: 0 body, 1 left edge, 2 right edge

    const int kHeaderW = 160, kRulerH = 28;
    double timeToX(double t) const;
    double xToTime(double x) const;
    int trackTop(int i) const;
    int trackAt(int y) const;
    QRect clipRect(const Clip &c, int trackIdx) const;
    QRect headerButton(int idx, int trackIdx) const;
    Hit hitTest(const QPoint &p) const;
    Clip effective(const Clip &c) const;
    double snapTime(double t, double dur, const QString &ignoreClip, bool useEnd = true) const;
    void updateScrollBars();
    void ensurePlayheadVisible();
    QPixmap thumbFor(const QString &mediaId, int h);
    void paintClip(QPainter &p, const Clip &c, int trackIdx, bool ghost);
    void paintWave(QPainter &p, const Clip &c, const QRect &r, const QVector<float> &peaks, const QColor &col);

    ProjectModel *m;
    double m_pps = 80;
    bool m_snap = true;
    Drag m_drag = Drag::None;
    QString m_dragClip, m_ghostTrack;
    QPoint m_pressPos;
    double m_origStart = 0, m_origIn = 0, m_origOut = 0, m_ghostStart = 0, m_ghostIn = 0, m_ghostOut = 0;
    int m_resizeTrack = -1, m_resizeOrigH = 0;
    bool m_dropActive = false;
    double m_dropStart = 0, m_dropDur = 0;
    QString m_dropTrack;
    QHash<QString, QPixmap> m_thumbCache;
};

} // namespace mv
