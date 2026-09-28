#include "ui/qt/inspect_panels.hpp"

#include <QAudioFormat>
#include <QAudioSink>
#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMediaDevices>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstring>

#include "devices/usb_audio_host.hpp"
#include "workbench/session.hpp"
#include "workbench/views.hpp"

namespace latasim::ui {

// ---- registers ----

RegisterPanel::RegisterPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    tree_ = new QTreeWidget;
    tree_->setObjectName("register_tree");
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({"register", "address", "value"});
    for (const auto& group : workbench::register_groups()) {
        auto* g = new QTreeWidgetItem(tree_, {QString::fromStdString(group.name)});
        for (const auto& r : group.registers)
            new QTreeWidgetItem(g, {QString::fromStdString(r.name), QString("0x%1").arg(r.address, 8, 16, QChar('0')), ""});
    }
    tree_->header()->setStretchLastSection(true);
    layout->addWidget(tree_);
}

void RegisterPanel::set_session(const workbench::Session* session) {
    session_ = session;
    refresh();
}

void RegisterPanel::refresh() {
    if (session_ == nullptr) return;
    const auto& groups = workbench::register_groups();
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        auto* group = tree_->topLevelItem(g);
        if (!group->isExpanded() && g != 0) continue;  // only what is on show
        for (int r = 0; r < group->childCount(); ++r) {
            const std::uint32_t v = workbench::register_value(*session_, groups[static_cast<std::size_t>(g)].registers[static_cast<std::size_t>(r)].address);
            group->child(r)->setText(2, QString("0x%1").arg(v, 8, 16, QChar('0')));
        }
    }
}

// ---- audio ----

AudioPanel::AudioPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    status_ = new QLabel;
    status_->setObjectName("audio_status");
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    auto* buttons = new QHBoxLayout;
    host_play_ = new QPushButton(tr("Pause PC stream"));
    host_play_->setObjectName("audio_host_play");
    connect(host_play_, &QPushButton::clicked, this, [this] {
        if (session_ == nullptr || session_->pc() == nullptr) return;
        auto* pc = session_->pc();
        pc->set_playing(!pc->playing());
        refresh();
    });
    buttons->addWidget(host_play_);
    auto* load = new QPushButton(tr("Load WAV as PC source..."));
    connect(load, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("PCM source"), {}, tr("WAV files (*.wav)"));
        if (!path.isEmpty()) load_wav(path);
    });
    buttons->addWidget(load);
    speaker_out_ = new QCheckBox(tr("Play the speaker on this PC"));
    speaker_out_->setObjectName("audio_host_output");
    connect(speaker_out_, &QCheckBox::toggled, this, [this](bool on) {
        if (!on && sink_ != nullptr) {
            sink_->stop();
            delete sink_;
            sink_ = nullptr;
            sink_io_ = nullptr;
            return;
        }
        if (on && sink_ == nullptr) {
            QAudioFormat format;
            format.setSampleRate(32'000);
            format.setChannelCount(1);
            format.setSampleFormat(QAudioFormat::Int16);
            sink_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
            sink_io_ = sink_->start();
            if (session_ != nullptr) played_ = session_->board().speaker().size();  // from now on
        }
    });
    buttons->addWidget(speaker_out_);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addStretch();
}

AudioPanel::~AudioPanel() = default;

void AudioPanel::set_session(workbench::Session* session) {
    session_ = session;
    played_ = session_ != nullptr ? session_->board().speaker().size() : 0;
    if (session_ != nullptr && session_->pc() != nullptr && wav_) session_->pc()->set_source(wav_.get());
    refresh();
}

bool AudioPanel::load_wav(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray d = f.readAll();
    auto u16 = [&](int i) { return static_cast<std::uint16_t>(static_cast<std::uint8_t>(d[i]) | static_cast<std::uint8_t>(d[i + 1]) << 8); };
    auto u32 = [&](int i) { return static_cast<std::uint32_t>(u16(i)) | static_cast<std::uint32_t>(u16(i + 2)) << 16; };
    if (d.size() < 44 || std::memcmp(d.constData(), "RIFF", 4) != 0 || std::memcmp(d.constData() + 8, "WAVE", 4) != 0) return false;
    int pos = 12, channels = 0, rate = 0, bits = 0;
    while (pos + 8 <= d.size()) {
        const std::uint32_t size = u32(pos + 4);
        if (std::memcmp(d.constData() + pos, "fmt ", 4) == 0) {
            if (u16(pos + 8) != 1) return false;  // PCM only
            channels = u16(pos + 10);
            rate = static_cast<int>(u32(pos + 12));
            bits = u16(pos + 22);
        } else if (std::memcmp(d.constData() + pos, "data", 4) == 0) {
            if (bits != 16 || channels < 1 || rate <= 0) return false;
            const int frames = static_cast<int>(std::min<std::uint32_t>(size, static_cast<std::uint32_t>(d.size() - pos - 8)) / (2u * static_cast<unsigned>(channels)));
            std::vector<std::int16_t> out;
            const std::int64_t n = static_cast<std::int64_t>(frames) * 32'000 / rate;  // nearest-sample resampling to 32 kHz
            for (std::int64_t i = 0; i < n; ++i) {
                const int frame = static_cast<int>(i * rate / 32'000);
                out.push_back(static_cast<std::int16_t>(u16(pos + 8 + frame * 2 * channels)));  // first channel
            }
            wav_ = std::make_unique<usb::BufferPcm>(std::move(out), true);
            if (session_ != nullptr && session_->pc() != nullptr) session_->pc()->set_source(wav_.get());
            return true;
        }
        pos += 8 + static_cast<int>(size + (size & 1));
    }
    return false;
}

void AudioPanel::play_new_samples() {
    if (sink_io_ == nullptr || session_ == nullptr) return;
    const auto& speaker = session_->board().speaker();
    if (played_ > speaker.size()) played_ = speaker.size();
    QByteArray bytes;
    for (; played_ < speaker.size(); ++played_) {
        const auto s = static_cast<std::int16_t>((static_cast<int>(speaker[played_].value) - 512) * 64);
        bytes.append(reinterpret_cast<const char*>(&s), 2);
    }
    if (!bytes.isEmpty()) sink_io_->write(bytes);
}

void AudioPanel::refresh() {
    if (session_ == nullptr) return;
    const auto& mcu = session_->board().mcu();
    const auto* pc = session_->pc();
    QString text;
    if (pc == nullptr) {
        text = tr("No PC on the USB port in this scenario.");
    } else {
        const auto& st = pc->stream();
        text = tr("PC: %1   frames %2, packets %3, samples sent %4\n")
                   .arg(QString::fromStdString(pc->state_name()))
                   .arg(pc->frames())
                   .arg(pc->packets_sent())
                   .arg(pc->samples_sent());
        if (st.sample_rate != 0)
            text += tr("Stream: interface %1 alt %2, endpoint %3 OUT, %4 Hz, %5 channel(s), %6-byte samples\n")
                        .arg(st.interface)
                        .arg(st.alternate)
                        .arg(st.endpoint)
                        .arg(st.sample_rate)
                        .arg(st.channels)
                        .arg(st.subframe);
        host_play_->setText(pc->playing() ? tr("Pause PC stream") : tr("Resume PC stream"));
    }
    text += tr("USB device: %1, address %2, frame %3\n")
                .arg(mcu.usb().pull_up() ? (mcu.usb().configured() ? tr("configured") : tr("connected")) : tr("not connected"))
                .arg(mcu.usb().address())
                .arg(mcu.usb().frame_number());
    text += tr("Speaker: %1 samples, DAC value %2").arg(session_->board().speaker().size()).arg(mcu.dac_value());
    status_->setText(text);
    play_new_samples();
}

}  // namespace latasim::ui
