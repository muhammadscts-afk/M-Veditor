#include <QDataStream>
#include <QFileInfo>
#include <algorithm>
#include <cstdlib>
#include "Dialogs.h"
#include "AppSettings.h"
#include "AudioAnalysis.h"
#include "ExportPanel.h"
#include "FfmpegTools.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <cmath>

namespace mv {

// ================================================================= SpeedDialog
SpeedDialog::SpeedDialog(double cur, double srcSec, bool hasAudio, bool pitch, bool rev, QWidget *parent) : QDialog(parent), m_src(srcSec) {
    setWindowTitle(tr("Speed controller"));
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("<b>Change the real speed of the clip.</b> The media is rendered at the new speed on export; the original file is never changed.")));
    auto *presets = new QHBoxLayout;
    m_spin = new QDoubleSpinBox; m_spin->setDecimals(2); m_spin->setRange(0.1, 100); m_spin->setSingleStep(0.1); m_spin->setSuffix(" ×"); m_spin->setValue(cur);
    m_spin->setToolTip(tr("Slow motion 0.1× – 0.9×, normal 1×, fast motion 1.1× – 16× and beyond"));
    for (double v : {0.1, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0}) {
        auto *b = new QPushButton(QString::number(v, 'g', 3) + "×"); b->setMaximumWidth(56);
        connect(b, &QPushButton::clicked, this, [this, v] { m_spin->setValue(v); });
        presets->addWidget(b);
    }
    lay->addLayout(presets);
    auto *f = new QFormLayout; f->addRow(tr("Custom speed"), m_spin); lay->addLayout(f);
    m_info = new QLabel; lay->addWidget(m_info);
    m_rev = new QCheckBox(tr("Play in reverse (clips up to 3 minutes)")); m_rev->setChecked(rev); lay->addWidget(m_rev);
    auto *g = new QGroupBox(tr("Audio")); auto *gl = new QVBoxLayout(g);
    m_pitch = new QCheckBox(tr("Preserve audio pitch")); m_pitch->setChecked(pitch);
    m_keep = new QRadioButton(tr("Keep audio (speed it up / slow it down too)")); m_keep->setChecked(true);
    m_mute = new QRadioButton(tr("Mute the clip's audio"));
    m_detach = new QRadioButton(tr("Detach audio to an audio track (keeps its original speed)"));
    gl->addWidget(m_pitch); gl->addWidget(m_keep); gl->addWidget(m_mute); gl->addWidget(m_detach);
    g->setEnabled(hasAudio); m_audioGroup = g;
    lay->addWidget(g);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this] { updateInfo(); });
    updateInfo();
}
void SpeedDialog::updateInfo() {
    const double s = m_spin->value();
    m_info->setText(tr("%1 → <b>%2</b> on the timeline  (%3)").arg(humanTime(m_src, true), humanTime(m_src / s, true),
                    s < 0.999 ? tr("slow motion") : s > 1.001 ? tr("fast motion") : tr("normal speed")));
}
void SpeedDialog::setSourceMode() { m_rev->hide(); m_audioGroup->hide(); setWindowTitle(tr("Preview speed (source)")); adjustSize(); }
double SpeedDialog::speed() const { return m_spin->value(); }
bool SpeedDialog::preservePitch() const { return m_pitch->isChecked(); }
bool SpeedDialog::reverse() const { return m_rev->isChecked(); }
SpeedDialog::AudioMode SpeedDialog::audioMode() const { return m_mute->isChecked() ? MuteAudio : m_detach->isChecked() ? DetachAudio : KeepAudio; }

// ================================================================= MicRecorder
static void writeWavHeader(QFile &f, int ch, int rate, qint64 dataBytes) {
    QByteArray h;
    QDataStream s(&h, QIODevice::WriteOnly); s.setByteOrder(QDataStream::LittleEndian);
    h.append("RIFF", 4); s << quint32(36 + dataBytes); h.append("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << quint16(ch) << quint32(rate) << quint32(rate * ch * 2) << quint16(ch * 2) << quint16(16);
    h.append("data", 4); s << quint32(dataBytes);
    f.seek(0); f.write(h);
}
bool MicRecorder::start(const QByteArray &deviceId, const QString &path, QString *err) {
    QAudioDevice dev = QMediaDevices::defaultAudioInput();
    for (auto &d : QMediaDevices::audioInputs()) if (d.id() == deviceId) dev = d;
    if (dev.isNull()) { if (err) *err = tr("No microphone was found. Connect one and check Windows Settings > Privacy > Microphone."); return false; }
    QAudioFormat fmt; fmt.setSampleFormat(QAudioFormat::Int16);
    fmt.setSampleRate(48000); fmt.setChannelCount(1);
    if (!dev.isFormatSupported(fmt)) { fmt = dev.preferredFormat(); fmt.setSampleFormat(QAudioFormat::Int16); if (!dev.isFormatSupported(fmt)) { if (err) *err = tr("The microphone format is not supported."); return false; } }
    m_channels = fmt.channelCount(); m_rate = fmt.sampleRate();
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly)) { if (err) *err = tr("Cannot create the recording file: %1").arg(m_file.errorString()); return false; }
    writeWavHeader(m_file, m_channels, m_rate, 0);
    m_bytes = 0;
    m_src = std::make_unique<QAudioSource>(dev, fmt);
    m_io = m_src->start();
    if (!m_io || m_src->error() != QAudio::NoError) {
        if (err) *err = tr("The microphone could not be opened. Windows may be blocking access: Settings > Privacy > Microphone.");
        m_src.reset(); m_file.close(); QFile::remove(path); return false;
    }
    connect(m_io, &QIODevice::readyRead, this, &MicRecorder::onData);
    return true;
}
void MicRecorder::onData() {
    const QByteArray d = m_io->readAll();
    if (d.isEmpty()) return;
    m_file.seek(m_file.size()); m_file.write(d); m_bytes += d.size();
    const qint16 *s = reinterpret_cast<const qint16 *>(d.constData());
    int mx = 0; for (int i = 0; i < d.size() / 2; ++i) mx = std::max(mx, std::abs(int(s[i])));
    emit level(mx / 32768.0);
}
void MicRecorder::stop() {
    if (!m_src) return;
    m_src->stop(); m_src.reset(); m_io = nullptr;
    writeWavHeader(m_file, m_channels, m_rate, m_bytes);
    m_file.close();
}

RecordDialog::RecordDialog(const QString &folder, QWidget *parent) : QDialog(parent), m_folder(folder) {
    setWindowTitle(tr("Record voice-over"));
    auto *lay = new QVBoxLayout(this);
    m_dev = new QComboBox;
    for (auto &d : QMediaDevices::audioInputs()) m_dev->addItem(d.description(), d.id());
    auto *f = new QFormLayout; f->addRow(tr("Microphone"), m_dev); lay->addLayout(f);
    m_level = new QProgressBar; m_level->setRange(0, 100); m_level->setTextVisible(false); lay->addWidget(m_level);
    m_time = new QLabel("00:00"); m_time->setAlignment(Qt::AlignCenter); m_time->setStyleSheet("font-size:22pt;"); lay->addWidget(m_time);
    m_msg = new QLabel(m_dev->count() ? tr("Press Record, speak, then press Stop. The recording is added to your project at the playhead.")
                                      : tr("No microphone found. Connect one and allow access in Windows Settings > Privacy > Microphone."));
    m_msg->setWordWrap(true); lay->addWidget(m_msg);
    m_btn = new QPushButton(tr("● Record")); m_btn->setEnabled(m_dev->count() > 0); lay->addWidget(m_btn);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close); lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_btn, &QPushButton::clicked, this, &RecordDialog::toggle);
    connect(&m_rec, &MicRecorder::level, this, [this](double v) { m_level->setValue(int(std::min(1.0, v * 1.4) * 100)); });
    connect(&m_tick, &QTimer::timeout, this, [this] { m_elapsed += 200; m_time->setText(humanTime(m_elapsed / 1000.0)); });
    connect(this, &QDialog::finished, this, [this] { if (m_rec.isRecording()) m_rec.stop(); });
}
void RecordDialog::toggle() {
    if (m_rec.isRecording()) {
        m_rec.stop(); m_tick.stop(); m_btn->setText(tr("● Record"));
        m_result = m_current; accept();
        return;
    }
    m_current = QDir(m_folder).absoluteFilePath("voice_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".wav");
    QString err;
    if (!m_rec.start(m_dev->currentData().toByteArray(), m_current, &err)) { m_msg->setText(err); return; }
    m_elapsed = 0; m_tick.start(200); m_btn->setText(tr("■ Stop and add to project")); m_msg->setText(tr("Recording…"));
}

// ================================================================= BatchDialog
BatchDialog::BatchDialog(bool audio, const QString &folder, QWidget *parent) : QDialog(parent), m_audio(audio) {
    setWindowTitle(audio ? tr("Batch audio conversion") : tr("Batch video conversion"));
    resize(560, 520);
    auto *lay = new QVBoxLayout(this);
    m_list = new QListWidget; m_list->setSelectionMode(QAbstractItemView::ExtendedSelection); m_list->setAcceptDrops(false);
    lay->addWidget(m_list, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add files…")), *rem = new QPushButton(tr("Remove selected"));
    row->addWidget(add); row->addWidget(rem); row->addStretch(1); lay->addLayout(row);
    auto *f = new QFormLayout;
    m_format = new QComboBox;
    for (auto &fi : exportFormats()) if (fi.video != audio) m_format->addItem(fi.label, fi.id);
    m_quality = new QComboBox; m_quality->addItem(tr("Draft"), "draft"); m_quality->addItem(tr("Standard"), "standard"); m_quality->addItem(tr("High"), "high"); m_quality->addItem(tr("Maximum"), "max"); m_quality->setCurrentIndex(1);
    m_res = new QComboBox; m_res->addItem(tr("Same as source"), 0); for (int h : {480, 720, 1080, 1440, 2160}) m_res->addItem(QString("%1p").arg(h), h);
    m_abr = new QComboBox; for (int b : {96, 128, 192, 256, 320}) m_abr->addItem(QString("%1 kbps").arg(b), b); m_abr->setCurrentIndex(2);
    f->addRow(tr("Convert to"), m_format);
    if (!audio) { f->addRow(tr("Resolution"), m_res); f->addRow(tr("Quality"), m_quality); }
    f->addRow(tr("Audio bitrate"), m_abr);
    m_dir = new QLineEdit(folder);
    auto *br = new QPushButton("…"); auto *dr = new QHBoxLayout; dr->addWidget(m_dir, 1); dr->addWidget(br); f->addRow(tr("Output folder"), dr);
    lay->addLayout(f);
    lay->addWidget(new QLabel(tr("Originals are never changed. Existing files are never overwritten; a number is added to the name instead.")));
    m_bar = new QProgressBar; m_bar->setRange(0, 1000); lay->addWidget(m_bar);
    m_status = new QLabel; m_status->setWordWrap(true); lay->addWidget(m_status);
    auto *bb = new QHBoxLayout; m_start = new QPushButton(tr("Start")); m_start->setObjectName("primary");
    m_cancel = new QPushButton(tr("Close")); bb->addStretch(1); bb->addWidget(m_start); bb->addWidget(m_cancel); lay->addLayout(bb);

    connect(add, &QPushButton::clicked, this, [this] {
        const QString filt = m_audio ? tr("Audio and video files (*.mp3 *.wav *.aac *.m4a *.flac *.ogg *.opus *.wma *.mp4 *.mov *.mkv *.avi *.webm);;All files (*)")
                                     : tr("Video files (*.mp4 *.mov *.avi *.mkv *.webm *.m4v *.wmv *.flv);;All files (*)");
        addFiles(QFileDialog::getOpenFileNames(this, tr("Add files"), AppSettings::importDir(), filt));
    });
    connect(rem, &QPushButton::clicked, this, [this] { qDeleteAll(m_list->selectedItems()); });
    connect(br, &QPushButton::clicked, this, [this] { const QString d = QFileDialog::getExistingDirectory(this, tr("Output folder"), m_dir->text()); if (!d.isEmpty()) m_dir->setText(d); });
    connect(m_start, &QPushButton::clicked, this, [this] {
        m_files.clear(); for (int i = 0; i < m_list->count(); ++i) m_files << m_list->item(i)->data(Qt::UserRole).toString();
        if (m_files.isEmpty()) { QMessageBox::information(this, windowTitle(), tr("Add some files first.")); return; }
        if (m_dir->text().trimmed().isEmpty()) { QMessageBox::information(this, windowTitle(), tr("Choose an output folder.")); return; }
        m_index = -1; m_ok = m_fail = 0; m_aborted = false; m_errors.clear(); m_start->setEnabled(false); m_cancel->setText(tr("Cancel"));
        startNext();
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_job && m_job->isRunning()) { m_aborted = true; m_job->cancel(); } else reject();
    });
}
void BatchDialog::addFiles(const QStringList &files) {
    for (auto &f : files) {
        bool dup = false; for (int i = 0; i < m_list->count(); ++i) if (m_list->item(i)->data(Qt::UserRole).toString() == f) dup = true;
        if (dup) continue;
        auto *it = new QListWidgetItem(QFileInfo(f).fileName()); it->setData(Qt::UserRole, f); it->setToolTip(QDir::toNativeSeparators(f)); m_list->addItem(it);
    }
}
void BatchDialog::startNext() {
    if (m_aborted || ++m_index >= m_files.size()) { finishAll(); return; }
    const QString file = m_files[m_index];
    m_status->setText(tr("Converting %1 of %2: %3").arg(m_index + 1).arg(m_files.size()).arg(QFileInfo(file).fileName()));
    ProbeResult pr = probeBlocking(file);
    if (!pr.ok) { ++m_fail; m_errors << QFileInfo(file).fileName() + ": " + pr.error; startNext(); return; }
    if (m_audio && !pr.item.hasAudio) { ++m_fail; m_errors << QFileInfo(file).fileName() + ": " + tr("no audio stream"); startNext(); return; }
    if (!m_audio && !pr.item.hasVideo) { ++m_fail; m_errors << QFileInfo(file).fileName() + ": " + tr("no video stream"); startNext(); return; }
    Project p = Project::singleClip(pr.item);
    ExportOptions o;
    o.format = m_format->currentData().toString();
    o.quality = m_quality->currentData().toString();
    o.audioBitrateKbps = m_abr->currentData().toInt();
    o.hwaccel = AppSettings::hwaccel();
    o.sampleRate = pr.item.sampleRate > 0 ? pr.item.sampleRate : 48000;
    o.channels = pr.item.channels == 1 ? 1 : 2;
    const int h = m_res->currentData().toInt();
    if (!m_audio && h > 0 && pr.item.height > 0) { o.height = h & ~1; o.width = int(std::lround(h * double(pr.item.width) / pr.item.height)) & ~1; }
    o.outputPath = ExportPanel::uniquePath(QDir(m_dir->text()).absoluteFilePath(QFileInfo(file).completeBaseName() + "." + formatById(o.format).ext));
    m_job = std::make_unique<ExportJob>();
    connect(m_job.get(), &ExportJob::progress, this, [this](double fr, double, const QString &) {
        m_bar->setValue(int((m_index + fr) / std::max<int>(1, m_files.size()) * 1000));
    });
    connect(m_job.get(), &ExportJob::finished, this, [this, file](bool ok, const QString &msg, const QString &) {
        if (ok) ++m_ok; else if (!m_aborted) { ++m_fail; m_errors << QFileInfo(file).fileName() + ": " + msg.section('\n', 0, 0); }
        QTimer::singleShot(0, this, [this] { startNext(); });
    });
    QString err;
    if (!m_job->start(p, o, &err)) { ++m_fail; m_errors << QFileInfo(file).fileName() + ": " + err.section('\n', 0, 0); startNext(); }
}
void BatchDialog::finishAll() {
    m_start->setEnabled(true); m_cancel->setText(tr("Close"));
    m_bar->setValue(m_aborted ? m_bar->value() : 1000);
    QString s = m_aborted ? tr("Cancelled. ") : QString();
    s += tr("%1 converted, %2 failed.").arg(m_ok).arg(m_fail);
    if (!m_errors.isEmpty()) s += "\n" + m_errors.join("\n");
    m_status->setText(s);
}

// ================================================================= SettingsDialog
SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    auto *lay = new QVBoxLayout(this);
    auto *f = new QFormLayout;
    auto *theme = new QComboBox; theme->addItem(tr("Dark"), true); theme->addItem(tr("Light"), false);
    theme->setCurrentIndex(AppSettings::darkTheme() ? 0 : 1);
    auto *autosave = new QSpinBox; autosave->setRange(0, 60); autosave->setSuffix(tr(" min")); autosave->setSpecialValueText(tr("Off")); autosave->setValue(AppSettings::autosaveMinutes());
    auto *proxies = new QCheckBox(tr("Use optimized proxy files for preview when available")); proxies->setChecked(AppSettings::useProxies());
    auto *restore = new QCheckBox(tr("Reopen the last project when the app starts")); restore->setChecked(AppSettings::restoreSession());
    auto *startup = new QCheckBox(tr("Start M Veditor when Windows starts"));
#ifdef Q_OS_WIN
    startup->setChecked(AppSettings::startupWithWindows());
#else
    startup->setEnabled(false);
#endif
    auto *cache = new QLineEdit(AppSettings::cacheDir());
    auto *cb = new QPushButton("…");
    auto *cr = new QHBoxLayout; cr->addWidget(cache, 1); cr->addWidget(cb);
    auto *cacheInfo = new QLabel(tr("Cache size: %1").arg(humanSize(WaveformCache::usedBytes())));
    auto *clear = new QPushButton(tr("Clear cache"));
    auto *ff = new QLineEdit(AppSettings::ffmpegDir()); ff->setPlaceholderText(tr("Bundled FFmpeg (recommended)"));
    f->addRow(tr("Theme"), theme); f->addRow(tr("Autosave"), autosave); f->addRow(proxies); f->addRow(restore); f->addRow(startup);
    f->addRow(tr("Cache folder"), cr); f->addRow(cacheInfo, clear); f->addRow(tr("FFmpeg folder (advanced)"), ff);
    lay->addLayout(f);
    lay->addWidget(new QLabel(tr("M Veditor works fully offline and sends no data. There is no automatic update check.")));
    lay->addWidget(new QLabel(AppSettings::portable() ? tr("Portable mode: settings are stored in the 'data' folder next to the program.") : tr("Settings are stored in your Windows user profile.")));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(cb, &QPushButton::clicked, this, [this, cache] { const QString d = QFileDialog::getExistingDirectory(this, tr("Cache folder"), cache->text()); if (!d.isEmpty()) cache->setText(d); });
    connect(clear, &QPushButton::clicked, this, [cacheInfo] { const qint64 n = WaveformCache::clearAll(); cacheInfo->setText(tr("Cache cleared (%1 freed)").arg(humanSize(n))); });
    connect(bb, &QDialogButtonBox::accepted, this, [=] {
        AppSettings::setDarkTheme(theme->currentData().toBool());
        AppSettings::setAutosaveMinutes(autosave->value());
        AppSettings::setUseProxies(proxies->isChecked());
        AppSettings::setRestoreSession(restore->isChecked());
        AppSettings::setCacheDir(cache->text()); WaveformCache::setCacheDir(cache->text());
        AppSettings::setFfmpegDir(ff->text()); tools::setOverrideDir(ff->text());
#ifdef Q_OS_WIN
        AppSettings::applyStartupWithWindows(startup->isChecked());
#endif
        accept();
    });
}

// ================================================================= MediaInfoDialog / TextViewer
MediaInfoDialog::MediaInfoDialog(const MediaItem &m, const QByteArray &raw, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Media information – %1").arg(m.name)); resize(560, 480);
    auto *lay = new QVBoxLayout(this);
    auto *tabs = new QTabWidget; lay->addWidget(tabs);
    auto *t = new QTableWidget; t->setColumnCount(2); t->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    t->horizontalHeader()->setStretchLastSection(true); t->verticalHeader()->setVisible(false); t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QVector<QPair<QString, QString>> rows = {{tr("File"), QDir::toNativeSeparators(m.path)}, {tr("Type"), m.kind}, {tr("Container"), m.container},
        {tr("Duration"), humanTime(m.duration, true)}, {tr("File size"), humanSize(m.sizeBytes)}, {tr("Overall bitrate"), m.bitrate ? QString("%1 kbps").arg(m.bitrate / 1000) : "-"}};
    if (m.hasVideo) rows += {{tr("Video codec"), m.videoCodec}, {tr("Resolution"), QString("%1 × %2").arg(m.width).arg(m.height)}, {tr("Frame rate"), QString::number(m.fps, 'f', 3) + " fps"}};
    if (m.hasAudio) rows += {{tr("Audio codec"), m.audioCodec}, {tr("Sample rate"), QString("%1 Hz").arg(m.sampleRate)}, {tr("Channels"), m.channels == 1 ? tr("Mono") : m.channels == 2 ? tr("Stereo") : QString::number(m.channels)}};
    t->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) { t->setItem(i, 0, new QTableWidgetItem(rows[i].first)); t->setItem(i, 1, new QTableWidgetItem(rows[i].second)); }
    t->resizeColumnToContents(0);
    tabs->addTab(t, tr("Summary"));
    auto *rawEdit = new QPlainTextEdit(QString::fromUtf8(QJsonDocument::fromJson(raw).toJson())); rawEdit->setReadOnly(true);
    tabs->addTab(rawEdit, tr("Technical (ffprobe)"));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close); lay->addWidget(bb); connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

TextViewer::TextViewer(const QString &title, const QString &md, QWidget *parent) : QDialog(parent) {
    setWindowTitle(title); resize(780, 640);
    auto *lay = new QVBoxLayout(this);
    auto *tb = new QTextBrowser; tb->setOpenExternalLinks(true); tb->setMarkdown(md); lay->addWidget(tb);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close); lay->addWidget(bb); connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace mv
