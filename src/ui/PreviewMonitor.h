// Preview monitor: source viewing, smooth rendered timeline preview, optional fast "live" approximation.
#pragma once
#include "ProjectModel.h"
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

namespace mv {

// A slider that jumps to the clicked position (a normal QSlider only moves by one page step).
class SeekSlider : public QSlider {
    Q_OBJECT
public:
    explicit SeekSlider(Qt::Orientation o, QWidget *parent = nullptr) : QSlider(o, parent) {}
signals:
    void seekRequested(double fraction);
    void dragStateChanged(bool down);
protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) return QSlider::mousePressEvent(e);
        m_down = true; emit dragStateChanged(true); seekTo(e->position().x()); e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override { if (m_down) { seekTo(e->position().x()); e->accept(); } }
    void mouseReleaseEvent(QMouseEvent *e) override { if (m_down) { m_down = false; emit dragStateChanged(false); e->accept(); } }
private:
    void seekTo(double x) {
        const int pad = 9;
        const int v = QStyle::sliderValueFromPosition(minimum(), maximum(), int(x) - pad, std::max(1, width() - 2 * pad));
        setValue(v);
        emit seekRequested(maximum() > 0 ? double(v) / maximum() : 0.0);
    }
    bool m_down = false;
};

class PreviewMonitor : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Source, Timeline, Rendered };
    explicit PreviewMonitor(ProjectModel *model, QWidget *parent = nullptr);
    void playSource(const QString &mediaId);
    void playRendered(const QString &path, double startSeconds, bool autoplay);
    void togglePlay();
    void pause();
    void stop();
    void stepFrame(int dir);
    bool isPlaying() const;
    Mode mode() const { return m_mode; }
    void releaseMedia();
    void showBusy(const QString &text);           // text drawn over the picture (empty = hide)
signals:
    void renderPreviewRequested();
    void timelinePlayRequested();
    void addToTimelineRequested(const QString &mediaId, double in, double out);
    void message(const QString &text);
protected:
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    struct Pending { bool valid = false; qint64 posMs = 0; bool play = false; };
    void setMode(Mode m);
    void setCanvas(int w, int h);
    void fit();
    void syncTo(double t, bool play);
    void applyToPlayer(qint64 posMs, bool play, double rate, double vol);
    void updateTitles(double t);
    void tick();
    void smoothTick();
    void updateTimeLabel();
    void onPlayheadChanged(double t, bool fromPlayback);
    void onProjectChanged();
    void onSeek(double fraction);
    const Clip *activeVideoClip(double t) const;

    ProjectModel *m;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    QGraphicsRectItem *m_canvas;
    QGraphicsVideoItem *m_video;
    QGraphicsPixmapItem *m_image;
    QGraphicsSimpleTextItem *m_busy;
    QVector<QGraphicsItem *> m_titleItems;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    SeekSlider *m_slider;
    QLabel *m_time, *m_modeLabel;
    QToolButton *m_play, *m_srcBtn, *m_tlBtn, *m_live;
    QWidget *m_srcTools;
    QTimer m_timer, m_smooth;
    QElapsedTimer m_clock, m_lastClock;
    Mode m_mode = Mode::Timeline;
    bool m_tlPlaying = false, m_sliderDrag = false;
    double m_t0 = 0;
    qint64 m_lastReported = 0;
    QString m_loadedPath, m_sourceId;
    Pending m_pending;
    double m_markIn = -1, m_markOut = -1, m_srcFps = 30;
    QString m_activeImageId;
    int m_cw = 1920, m_ch = 1080;
};

} // namespace mv
