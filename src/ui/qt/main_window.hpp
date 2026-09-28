#pragma once
// The Latasim workbench window (docs/phase5/desktop.md). It observes and controls
// one workbench::Session; the session, not the window, holds all simulation state.
//
// Runs happen on the UI thread in bounded slices of virtual time from a timer, so
// the window stays responsive; nothing else touches the session concurrently.
#include <QMainWindow>
#include <QTimer>

#include <cstdint>
#include <memory>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace latasim::workbench {
class Session;
}

namespace latasim::ui {

class AudioPanel;
class BoardPanel;
class GlcdView;
class RegisterPanel;
class RtosPanel;
class TimelineView;
class TracePanel;

class MainWindow : public QMainWindow {
public:
    MainWindow();
    ~MainWindow() override;

    void select_scenario(int index);  // a new session: the reset
    void run_for(std::uint64_t cycles);
    void run_until(std::uint64_t cycle);
    void step();
    void set_running(bool running);
    bool running() const { return timer_.isActive(); }
    void refresh();

    workbench::Session* session() { return session_.get(); }
    BoardPanel* board_panel() { return board_; }
    GlcdView* glcd_view() { return glcd_; }
    TracePanel* trace_panel() { return trace_; }
    RtosPanel* rtos_panel() { return rtos_; }
    TimelineView* timeline_view() { return timeline_; }
    RegisterPanel* register_panel() { return registers_; }
    AudioPanel* audio_panel() { return audio_; }

private:
    void tick();
    void after_run();

    std::unique_ptr<workbench::Session> session_;
    QTimer timer_;
    QComboBox* scenario_ = nullptr;
    QComboBox* speed_ = nullptr;
    QComboBox* amount_ = nullptr;
    QSpinBox* until_ms_ = nullptr;
    QPushButton* run_ = nullptr;
    QLabel* time_ = nullptr;
    QLabel* check_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* fault_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    BoardPanel* board_ = nullptr;
    GlcdView* glcd_ = nullptr;
    TracePanel* trace_ = nullptr;
    RtosPanel* rtos_ = nullptr;
    TimelineView* timeline_ = nullptr;
    RegisterPanel* registers_ = nullptr;
    AudioPanel* audio_ = nullptr;
};

}  // namespace latasim::ui
