// Preview monitor: source viewing, smooth LIVE timeline playback (double-buffered video + audio tracks),
// optional exact rendered preview and direct play of a single clip.
#pragma once
#include "PitchAudio.h"
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
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
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
    void setLoop(double a, double b) { m_la = a; m_lb = b; update(); }      // A-B repeat range as fractions (negative = none)
signals:
    void seekRequested(double fraction);
    void dragStateChanged(bool down);
protected:
    void paintEvent(QPaintEvent *e) override {
        QSlider::paintEvent(e);
        if (m_la >= 0 && m_lb >= m_la) {
            QPainter p(this); const double pad = 9, w = width() - 2 * pad;
            const QRectF r(pad + m_la * w, height() / 2.0 - 7, std::max(2.0, (m_lb - m_la) * w), 14);
            p.fillRect(r, QColor(255, 200, 40, 120)); p.fillRect(QRectF(r.left() - 1, r.top() - 2, 2, r.height() + 4), QColor(255, 200, 40));
            p.fillRect(QRectF(r.right() - 1, r.top() - 2, 2, r.height() + 4), QColor(255, 200, 40));
        }
    }
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
    double m_la = -1, m_lb = -1;
};

QIcon uiIcon(const QString &kind, const QColor &color);      // small icons drawn in any colour (the style's own icons are dark and vanish on a dark bar)

// Speaker button (click = mute) with a volume slider.
class VolumeBox : public QWidget {
    Q_OBJECT
public:
    explicit VolumeBox(const QColor &iconColor, QWidget *parent = nullptr);
    void setVolume(int percent);
    void setMuted(bool muted);
signals:
    void volumeChanged(int percent);
    void muteToggled(bool muted);
private:
    void refresh();
    QToolButton *m_btn;
    QSlider *m_slider;
    QColor m_color;
    bool m_muted = false;
    int m_pct = 100;
};

// Full-screen picture: the monitor's own view is moved into this window, so the picture always fills the screen.
// The seek bar appears when the mouse moves and hides again when it is idle (like other media players).
class FullscreenWindow : public QWidget {
    Q_OBJECT
public:
    explicit FullscreenWindow(QGraphicsView *view);
    SeekSlider *slider = nullptr;
    QLabel *time = nullptr;
    QToolButton *play = nullptr, *abBtn = nullptr;
    VolumeBox *vol = nullptr;
    bool barVisible() const { return m_bar->isVisible(); }
    QWidget *viewport() const { return m_view->viewport(); }
    void showBar();
signals:
    void exitRequested();
    void togglePlayRequested();
protected:
    void keyPressEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void closeEvent(QCloseEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    void hideBar();
    QGraphicsView *m_view = nullptr;
    QWidget *m_bar = nullptr;
    QTimer m_hide;
};

class PreviewMonitor : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Source, Timeline, Rendered };
    explicit PreviewMonitor(ProjectModel *model, QWidget *parent = nullptr);
    void playSource(const QString &mediaId);
    void playRendered(const QString &path, double startSeconds, bool autoplay);
    void playDirect(const QString &path, double in, double out, double startOnTimeline, bool autoplay);
    void togglePlay();
    void pause();
    void stop();
    void stepFrame(int dir);
    void seekRelative(double seconds);          // jump forward/backward without stopping playback
    void goStart();
    void goEnd();
    bool isPlaying() const;
    Mode mode() const { return m_mode; }
    bool liveEnabled() const { return m_live->isChecked(); }
    void releaseMedia();
    // Source mode: preview speed of the library item (pitch preserved)
    void setSourceSpeed(double s);
    double sourceSpeed() const { return m_srcSpeed; }
    QString sourceId() const { return m_sourceId; }
    void toggleFullscreen();
    bool isFullscreen() const { return m_fs != nullptr; }
    QGraphicsView *view() const { return m_view; }
    // volume (applies to everything the preview plays)
    void setMasterVolume(int percent);
    void setMuted(bool muted);
    int volumePercent() const { return int(m_master * 100 + 0.5); }
    bool isMuted() const { return m_muted; }
    float outputVolume() const { return m_audio->volume(); }
    // A-B repeat (like VLC): 1st click sets A, 2nd sets B and repeats, 3rd stops
    void abClick();
    int abState() const { return m_ab; }
    FullscreenWindow *fullscreenWindow() const { return m_fs; }
    void showBusy(const QString &text);
signals:
    void renderPreviewRequested();
    void timelinePlayRequested();      // Live is off: the main window renders / plays an exact preview
    void stopPressed();
    void addToTimelineRequested(const QString &mediaId, double in, double out);
    void message(const QString &text);
    void modeChanged();
    void extractAllRequested();
protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    struct Pending { bool valid = false; qint64 posMs = 0; bool play = false; double rate = 1.0, vol = 1.0; };
    struct Preload { QMediaPlayer *pl = nullptr; QAudioOutput *ao = nullptr; QGraphicsVideoItem *vi = nullptr;
                     QString clipId, path; Pending pend; bool loaded = false; qint64 lastPos = 0; };
    struct Voice { QMediaPlayer *pl = nullptr; QAudioOutput *ao = nullptr; QString clipId, path; Pending pend; bool loaded = false; };

    void wire(QMediaPlayer *pl);
    void onPosition(QMediaPlayer *pl, qint64 pos);
    void onState(QMediaPlayer *pl, QMediaPlayer::PlaybackState s);
    void onStatus(QMediaPlayer *pl, QMediaPlayer::MediaStatus st);
    void setMode(Mode m);
    void setCanvas(int w, int h);
    void fit();
    // live engine
    void syncTo(double t, bool play);
    double liveTime();
    void loadActive(const Clip &c, const MediaItem &mi, qint64 target, bool play, double rate, double vol);
    void keepPlaying(qint64 target);
    void preloadDeck(const Clip &c);
    void swapDecks();
    void syncVoices(double t, bool play);
    void ensureVoices();
    void releaseLive();
    void syncSourceAudio();
    double mvol(double v) const { return std::clamp(v * (m_muted ? 0.0 : m_master), 0.0, 1.0); }
    void applyMasterNow();
    void abClear();
    void abTick();
    void updateAbUi();
    void styleAb(QToolButton *b);
    double abPosition() const;
    double abDuration() const;
    void updateBadge();
    void enterFullscreen();
    void exitFullscreen();
    void stopPitch();
    double pitchAudioFor(const Clip &c, const MediaItem &mi, double t, bool play, double vol);   // returns the volume for the deck's own audio
    const Clip *nextVideoClip(double after) const;
    void updateLiveNote();
    void applyToPlayer(qint64 posMs, bool play, double rate, double vol);
    void updateTitles(double t);
    void tick();
    void smoothTick();
    void updateTimeLabel();
    void onPlayheadChanged(double t, bool fromPlayback);
    void onProjectChanged();
    void onSeek(double fraction);
    qint64 rangeMs() const;
    const Clip *activeVideoClip(double t, bool hiddenTracks = false) const;

    ProjectModel *m;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    QGraphicsRectItem *m_canvas;
    QGraphicsVideoItem *m_video;                 // the ACTIVE deck's picture
    QGraphicsPixmapItem *m_image;
    QGraphicsSimpleTextItem *m_busy, *m_note, *m_badge;
    double m_srcSpeed = 1.0;
    QTimer m_srcTimer, m_fsSync;
    FullscreenWindow *m_fs = nullptr;
    VolumeBox *m_volBox = nullptr;
    QToolButton *m_abBtn = nullptr, *m_extractBtn = nullptr;
    QTimer m_abTimer;
    int m_ab = 0;
    double m_abA = 0, m_abB = 0, m_master = 1.0;
    bool m_muted = false;
    double m_pitchSpeedSeen = 0;
    QElapsedTimer m_pitchSeenClock, m_srcSettle;
    QVector<QGraphicsItem *> m_titleItems;
    QString m_titleKey;
    QMediaPlayer *m_player;                      // the ACTIVE deck's player (swapped with m_pre on gap-free cuts)
    QAudioOutput *m_audio;
    Preload m_pre;                               // the second deck: preloads the next clip
    QVector<Voice> m_voices;                     // audio-track players (audio, music, voice-over)
    QString m_activeClipId;
    PitchAudio *m_pitch = nullptr;
    QString m_pitchClipId;
    double m_pitchSpeed = 0, m_pitchStartSrc = 0, m_activeRate = 1.0;
    QElapsedTimer m_pitchClock;
    SeekSlider *m_slider;
    QLabel *m_time, *m_modeLabel;
    QToolButton *m_play, *m_srcBtn, *m_tlBtn, *m_live;
    QWidget *m_srcTools;
    QTimer m_timer, m_smooth;
    QElapsedTimer m_clock, m_lastClock;
    Mode m_mode = Mode::Timeline;
    bool m_tlPlaying = false, m_sliderDrag = false;
    double m_t0 = 0;
    qint64 m_lastReported = 0, m_offsetMs = 0, m_endMs = 0, m_lastLiveMs = 0;
    QByteArray m_renderedSig;
    QString m_loadedPath, m_sourceId;
    Pending m_pending;
    double m_markIn = -1, m_markOut = -1, m_srcFps = 30;
    QString m_activeImageId;
    int m_cw = 1920, m_ch = 1080;
};

} // namespace mv
