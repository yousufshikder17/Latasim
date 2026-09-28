#pragma once
// Register inspection and the audio panel.
//
// Registers are read with Lpc1768::peek32, which has no side effects, so
// inspecting STCTRL, ADGDR or USBRxData changes nothing the firmware will see.
//
// Audio: the USB and playback state, host controls (play/pause the virtual PC,
// load a WAV file as its source) and optional playback of the speaker's samples
// through the PC's sound device. Host playback only reads the recorded samples:
// muting or unmuting it changes nothing in the simulation or its trace.
#include <QWidget>

#include <memory>
#include <vector>

class QAudioSink;
class QCheckBox;
class QIODevice;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace latasim::usb {
class BufferPcm;
}
namespace latasim::workbench {
class Session;
}

namespace latasim::ui {

class RegisterPanel : public QWidget {
public:
    explicit RegisterPanel(QWidget* parent = nullptr);
    void set_session(const workbench::Session* session);
    void refresh();
    QTreeWidget* tree() { return tree_; }

private:
    const workbench::Session* session_ = nullptr;
    QTreeWidget* tree_ = nullptr;
};

class AudioPanel : public QWidget {
public:
    explicit AudioPanel(QWidget* parent = nullptr);
    ~AudioPanel() override;
    void set_session(workbench::Session* session);
    void refresh();
    bool load_wav(const QString& path);  // 16-bit PCM WAV, any rate/channels: to 32 kHz mono

private:
    void play_new_samples();

    workbench::Session* session_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* host_play_ = nullptr;
    QCheckBox* speaker_out_ = nullptr;
    std::unique_ptr<usb::BufferPcm> wav_;
    QAudioSink* sink_ = nullptr;
    QIODevice* sink_io_ = nullptr;
    std::size_t played_ = 0;  // speaker samples already sent to the sound device
};

}  // namespace latasim::ui
