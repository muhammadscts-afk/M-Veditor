#pragma once
#include "ExportJob.h"
#include "MediaProbe.h"
#include <QAudioSource>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <memory>

namespace mv {

// ---------------------------------------------------------------- speed controller
class SpeedDialog : public QDialog {
    Q_OBJECT
public:
    enum AudioMode { KeepAudio, MuteAudio, DetachAudio };
    SpeedDialog(double currentSpeed, double sourceSeconds, bool hasAudio, bool preservePitch, bool reversed, QWidget *parent = nullptr);
    void setSourceMode();                    // plain preview speed: hides reverse and audio options
    double speed() const;
    bool preservePitch() const;
    bool reverse() const;
    AudioMode audioMode() const;
private:
    void updateInfo();
    QDoubleSpinBox *m_spin;
    QCheckBox *m_pitch, *m_rev;
    QRadioButton *m_keep, *m_mute, *m_detach;
    QLabel *m_info;
    QWidget *m_audioGroup = nullptr;
    double m_src;
};

// ---------------------------------------------------------------- microphone
class MicRecorder : public QObject {
    Q_OBJECT
public:
    explicit MicRecorder(QObject *parent = nullptr) : QObject(parent) {}
    bool start(const QByteArray &deviceId, const QString &wavPath, QString *error);
    void stop();
    bool isRecording() const { return m_src != nullptr; }
signals:
    void level(double peak01);
private:
    void onData();
    std::unique_ptr<QAudioSource> m_src;
    QIODevice *m_io = nullptr;
    QFile m_file;
    qint64 m_bytes = 0;
    int m_channels = 1, m_rate = 48000;
};

class RecordDialog : public QDialog {
    Q_OBJECT
public:
    explicit RecordDialog(const QString &folder, QWidget *parent = nullptr);
    QString recordedFile() const { return m_result; }
private:
    void toggle();
    QComboBox *m_dev;
    QPushButton *m_btn;
    QProgressBar *m_level;
    QLabel *m_time, *m_msg;
    MicRecorder m_rec;
    QTimer m_tick;
    qint64 m_elapsed = 0;
    QString m_folder, m_current, m_result;
};

// ---------------------------------------------------------------- batch conversion
class BatchDialog : public QDialog {
    Q_OBJECT
public:
    BatchDialog(bool audio, const QString &defaultFolder, QWidget *parent = nullptr);
    void addFiles(const QStringList &files);
private:
    void startNext();
    void finishAll();
    bool m_audio;
    QListWidget *m_list;
    QComboBox *m_format, *m_quality, *m_res, *m_abr;
    QLineEdit *m_dir;
    QPushButton *m_start, *m_cancel;
    QProgressBar *m_bar;
    QLabel *m_status;
    std::unique_ptr<ExportJob> m_job;
    int m_index = -1, m_ok = 0, m_fail = 0;
    bool m_aborted = false;
    QStringList m_files, m_errors;
};

// ---------------------------------------------------------------- settings / info / text
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
};

class MediaInfoDialog : public QDialog {
    Q_OBJECT
public:
    MediaInfoDialog(const MediaItem &m, const QByteArray &rawJson, QWidget *parent = nullptr);
};

class TextViewer : public QDialog {
    Q_OBJECT
public:
    TextViewer(const QString &title, const QString &markdown, QWidget *parent = nullptr);
};

} // namespace mv
