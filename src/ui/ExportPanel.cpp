#include <algorithm>
#include "ExportPanel.h"
#include "AppSettings.h"
#include "FfmpegTools.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace mv {

static bool formatAvailable(const QString &id) {
    using namespace tools;
    if (id == "mp4-h264" || id == "mov")
        return hasEncoder("libx264") || hasEncoder("h264_mf") || hasEncoder("h264_nvenc") || hasEncoder("h264_qsv") || hasEncoder("h264_amf");
    if (id == "mp4-h265") return hasEncoder("libx265") || hasEncoder("hevc_nvenc") || hasEncoder("hevc_qsv") || hasEncoder("hevc_amf");
    if (id == "webm") return hasEncoder("libvpx-vp9");
    if (id == "mp3") return hasEncoder("libmp3lame");
    return true;
}

ExportPanel::ExportPanel(ProjectModel *model, QWidget *parent) : QWidget(parent), m(model) {
    auto *lay = new QVBoxLayout(this);
    auto *f = new QFormLayout;
    m_format = new QComboBox;
    for (auto &fi : exportFormats()) m_format->addItem(fi.label, fi.id);
    if (auto *sm = qobject_cast<QStandardItemModel *>(m_format->model()))
        for (int i = 0; i < m_format->count(); ++i)
            if (!formatAvailable(m_format->itemData(i).toString())) {
                sm->item(i)->setEnabled(false);
                sm->item(i)->setToolTip(tr("Not available in this FFmpeg build"));
            }
    f->addRow(tr("Format"), m_format);

    m_videoBox = new QWidget; auto *vf = new QFormLayout(m_videoBox); vf->setContentsMargins(0, 0, 0, 0);
    m_res = new QComboBox;
    m_res->addItem(tr("Project size"), 0);
    for (int h : {480, 720, 1080, 1440, 2160}) m_res->addItem(h == 2160 ? "4K (2160p)" : QString("%1p").arg(h), h);
    m_res->setCurrentIndex(m_res->findData(1080));
    m_fps = new QComboBox; m_fps->addItem(tr("Project rate"), 0.0); for (double v : {24.0, 25.0, 30.0, 50.0, 60.0}) m_fps->addItem(QString("%1 fps").arg(v), v);
    m_quality = new QComboBox; m_quality->addItem(tr("Draft (small file)"), "draft"); m_quality->addItem(tr("Standard"), "standard"); m_quality->addItem(tr("High"), "high"); m_quality->addItem(tr("Maximum"), "max");
    m_quality->setCurrentIndex(1);
    m_vbr = new QSpinBox; m_vbr->setRange(0, 200000); m_vbr->setSingleStep(500); m_vbr->setSuffix(" kbps"); m_vbr->setSpecialValueText(tr("Auto")); m_vbr->setMinimumWidth(70);
    m_vbr->setToolTip(tr("Leave on Automatic for constant quality, or set a target video bitrate"));
    m_hw = new QComboBox; m_hw->addItem(tr("Software (most compatible)"), "none"); m_hw->addItem(tr("Automatic hardware"), "auto");
    m_hw->addItem("NVIDIA NVENC", "nvenc"); m_hw->addItem("Intel Quick Sync", "qsv"); m_hw->addItem("AMD AMF", "amf");
    m_hw->setToolTip(tr("If a hardware encoder fails, M Veditor automatically retries with the software encoder."));
    m_hw->setCurrentIndex(std::max(0, m_hw->findData(AppSettings::hwaccel())));
    vf->addRow(tr("Resolution"), m_res); vf->addRow(tr("Frame rate"), m_fps); vf->addRow(tr("Quality"), m_quality);
    vf->addRow(tr("Video bitrate"), m_vbr); vf->addRow(tr("Acceleration"), m_hw);
    f->addRow(m_videoBox);

    m_abr = new QComboBox; for (int b : {64, 96, 128, 160, 192, 256, 320}) m_abr->addItem(QString("%1 kbps").arg(b), b);
    m_abr->setCurrentIndex(m_abr->findData(192));
    m_sr = new QComboBox; for (int s : {22050, 32000, 44100, 48000, 96000}) m_sr->addItem(QString("%1 Hz").arg(s), s);
    m_sr->setCurrentIndex(m_sr->findData(48000));
    m_ch = new QComboBox; m_ch->addItem(tr("Stereo"), 2); m_ch->addItem(tr("Mono"), 1);
    f->addRow(tr("Audio bitrate"), m_abr); f->addRow(tr("Sample rate"), m_sr); f->addRow(tr("Channels"), m_ch);

    m_dir = new QLineEdit(AppSettings::exportDir());
    auto *br = new QToolButton; br->setText("…"); br->setToolTip(tr("Choose the output folder"));
    auto *dr = new QHBoxLayout; dr->addWidget(m_dir, 1); dr->addWidget(br);
    f->addRow(tr("Folder"), dr);
    m_name = new QLineEdit("My video"); f->addRow(tr("File name"), m_name);
    m_copy = new QCheckBox(tr("Fast export, no re-encoding"));
    m_copy->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_copy->setToolTip(tr("Keeps the original quality and is very fast. Only available for a single, unmodified clip. Cuts happen at the nearest keyframe."));
    m_info = new QLabel; m_info->setWordWrap(true);
    lay->addLayout(f);
    lay->addWidget(m_copy);
    lay->addWidget(m_info);
    m_go = new QPushButton(tr("Export")); m_go->setObjectName("primary");
    m_cancel = new QPushButton(tr("Cancel")); m_cancel->setVisible(false);
    m_bar = new QProgressBar; m_bar->setRange(0, 1000); m_bar->setVisible(false);
    m_status = new QLabel; m_status->setWordWrap(true);
    auto *row = new QHBoxLayout; row->addWidget(m_go, 1); row->addWidget(m_cancel);
    lay->addLayout(row); lay->addWidget(m_bar); lay->addWidget(m_status);
    lay->addStretch(1);

    f->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (auto *cb : findChildren<QComboBox *>()) { cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); cb->setMinimumContentsLength(6); cb->setMinimumWidth(80); }
    m_dir->setMinimumWidth(40); m_name->setMinimumWidth(40);
    connect(br, &QToolButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Output folder"), m_dir->text());
        if (!d.isEmpty()) { m_dir->setText(d); AppSettings::setExportDir(d); }
    });
    connect(m_dir, &QLineEdit::editingFinished, this, [this] { AppSettings::setExportDir(m_dir->text()); });
    connect(m_format, &QComboBox::currentIndexChanged, this, &ExportPanel::onFormatChanged);
    connect(m_hw, &QComboBox::currentIndexChanged, this, [this] { AppSettings::setHwaccel(m_hw->currentData().toString()); });
    for (QComboBox *c : {m_res, m_fps, m_sr, m_ch, m_quality, m_abr}) connect(c, &QComboBox::currentIndexChanged, this, &ExportPanel::updateReencodeInfo);
    connect(m_copy, &QCheckBox::toggled, this, &ExportPanel::updateReencodeInfo);
    connect(m, &ProjectModel::changed, this, &ExportPanel::updateReencodeInfo);
    connect(m_go, &QPushButton::clicked, this, &ExportPanel::exportRequested);
    connect(m_cancel, &QPushButton::clicked, this, &ExportPanel::cancelRequested);
    onFormatChanged();
}

void ExportPanel::selectFormat(const QString &id) { int i = m_format->findData(id); if (i >= 0) m_format->setCurrentIndex(i); }

void ExportPanel::onFormatChanged() {
    const FormatInfo fi = formatById(m_format->currentData().toString());
    m_videoBox->setVisible(fi.video);
    m_abr->setEnabled(fi.id != "wav" && fi.id != "flac");
    updateReencodeInfo();
}

ExportOptions ExportPanel::currentOptions() const {
    ExportOptions o;
    o.format = m_format->currentData().toString();
    const FormatInfo fi = formatById(o.format);
    const Project &p = m->project();
    const int h = m_res->currentData().toInt();
    if (fi.video && h > 0) {
        const double aspect = double(p.settings.width) / std::max(1, p.settings.height);
        o.height = h & ~1; o.width = int(std::lround(h * aspect)) & ~1;
    }
    o.fps = m_fps->currentData().toDouble();
    o.quality = m_quality->currentData().toString();
    o.videoBitrateKbps = m_vbr->value();
    o.audioBitrateKbps = m_abr->currentData().toInt();
    o.sampleRate = m_sr->currentData().toInt();
    o.channels = m_ch->currentData().toInt();
    o.hwaccel = m_hw->currentData().toString();
    o.streamCopy = m_copy->isChecked();
    QString name = m_name->text().trimmed();
    if (name.isEmpty()) name = "export";
    for (QChar c : QString("<>:\"/\\|?*")) name.replace(c, '_');
    o.outputPath = QDir(m_dir->text()).absoluteFilePath(name + "." + fi.ext);
    if (o.streamCopy) { o.sampleRate = 0; o.channels = 0; o.width = o.height = 0; o.fps = 0; }
    return o;
}

void ExportPanel::updateReencodeInfo() {
    const Project &p = m->project();
    // Stream copy ignores size / rate / quality settings, so evaluate it against the source as-is.
    ExportOptions probe = currentOptions();
    probe.streamCopy = true; probe.width = probe.height = 0; probe.fps = 0; probe.sampleRate = 0; probe.channels = 0;
    const QString why = RenderGraph::reencodeReason(p, probe);
    const bool possible = why.isEmpty();
    m_copy->setEnabled(possible || m_copy->isChecked());
    if (m_copy->isChecked() && !possible) m_copy->setChecked(false);
    if (p.clips.isEmpty()) m_info->setText(tr("Add clips to the timeline to export."));
    else if (m_copy->isChecked()) m_info->setText(tr("<b>No re-encoding:</b> the original picture and sound are copied as they are (cuts snap to the nearest keyframe)."));
    else if (possible) m_info->setText(tr("<b>Re-encoding:</b> the result will be rendered again. Tick the box above for a faster, lossless export of this clip."));
    else m_info->setText(tr("<b>Re-encoding is required.</b> %1").arg(why.section(": ", 1)));
}

void ExportPanel::setBusy(bool busy) {
    m_go->setEnabled(!busy); m_cancel->setVisible(busy); m_bar->setVisible(busy);
    if (!busy) m_bar->setValue(0);
}
void ExportPanel::setProgress(double f, const QString &text) { m_bar->setValue(int(f * 1000)); m_status->setText(text); }

QString ExportPanel::uniquePath(const QString &path) {
    if (!QFileInfo::exists(path)) return path;
    QFileInfo fi(path);
    for (int i = 1; i < 10000; ++i) {
        const QString c = fi.absolutePath() + "/" + fi.completeBaseName() + QString(" (%1).").arg(i) + fi.suffix();
        if (!QFileInfo::exists(c)) return c;
    }
    return path;
}

} // namespace mv
