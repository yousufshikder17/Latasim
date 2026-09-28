#include "ui/qt/main_window.hpp"

#include <QComboBox>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include "ui/qt/board_panel.hpp"
#include "ui/qt/glcd_view.hpp"
#include "ui/qt/inspect_panels.hpp"
#include "ui/qt/rtos_panel.hpp"
#include "ui/qt/trace_panel.hpp"
#include "workbench/session.hpp"

namespace latasim::ui {
namespace {

constexpr std::uint64_t kMs = 100'000;  // core cycles per millisecond
constexpr int kTickMs = 20;             // run-timer interval (wall clock, UI pacing only)

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle("Latasim workbench");

    auto* bar = addToolBar(tr("Simulation"));
    bar->setObjectName("simulation_toolbar");
    scenario_ = new QComboBox;
    scenario_->setObjectName("scenario");
    for (const auto& s : workbench::scenarios()) scenario_->addItem(QString::fromStdString(s.name));
    bar->addWidget(scenario_);
    auto* reset = new QPushButton(tr("Reset"));
    reset->setObjectName("reset");
    bar->addWidget(reset);
    bar->addSeparator();
    run_ = new QPushButton(tr("Run"));
    run_->setObjectName("run");
    run_->setCheckable(true);
    bar->addWidget(run_);
    speed_ = new QComboBox;
    speed_->setObjectName("speed");
    speed_->addItems({tr("real time"), tr("10x"), tr("as fast as possible")});
    bar->addWidget(speed_);
    auto* step = new QPushButton(tr("Step event"));
    step->setObjectName("step");
    step->setToolTip(tr("Advance virtual time to the next scheduled hardware event (a SysTick, a timer match, an "
                        "ADC conversion, a USB frame). Not an instruction step: host firmware has no instructions."));
    bar->addWidget(step);
    bar->addSeparator();
    amount_ = new QComboBox;
    amount_->setObjectName("run_for_amount");
    amount_->addItems({"1 ms", "10 ms", "100 ms", "1 s"});
    bar->addWidget(amount_);
    auto* run_for = new QPushButton(tr("Run for"));
    run_for->setObjectName("run_for");
    bar->addWidget(run_for);
    until_ms_ = new QSpinBox;
    until_ms_->setObjectName("run_until_ms");
    until_ms_->setRange(0, 3'600'000);
    until_ms_->setSuffix(" ms");
    bar->addWidget(until_ms_);
    auto* run_until = new QPushButton(tr("Run until"));
    run_until->setObjectName("run_until");
    bar->addWidget(run_until);
    bar->addSeparator();
    time_ = new QLabel;
    time_->setObjectName("time");
    time_->setMinimumWidth(260);
    bar->addWidget(time_);

    auto* central = new QWidget;
    auto* v = new QVBoxLayout(central);
    description_ = new QLabel;
    description_->setWordWrap(true);
    check_ = new QLabel;
    check_->setObjectName("check");
    auto* header = new QHBoxLayout;
    header->addWidget(description_, 1);
    header->addWidget(check_);
    v->addLayout(header);

    auto* split = new QSplitter(Qt::Vertical);
    auto* top = new QSplitter(Qt::Horizontal);
    board_ = new BoardPanel;
    top->addWidget(board_);
    auto* glcd_box = new QGroupBox(tr("GLCD (320 x 240, RGB565)"));
    auto* glcd_layout = new QVBoxLayout(glcd_box);
    glcd_ = new GlcdView;
    glcd_->setObjectName("glcd");
    auto* scale = new QComboBox;
    scale->addItems({tr("fit (whole multiples)"), "1x", "2x", "3x"});
    connect(scale, &QComboBox::currentIndexChanged, this, [this](int i) { glcd_->set_scale(i); });
    glcd_layout->addWidget(scale);
    glcd_layout->addWidget(glcd_, 1);
    top->addWidget(glcd_box);
    auto* right = new QWidget;
    auto* right_layout = new QVBoxLayout(right);
    auto* audio_box = new QGroupBox(tr("USB audio"));
    auto* audio_layout = new QVBoxLayout(audio_box);
    audio_ = new AudioPanel;
    audio_layout->addWidget(audio_);
    right_layout->addWidget(audio_box);
    status_ = new QLabel;
    status_->setObjectName("scenario_status");
    status_->setWordWrap(true);
    right_layout->addWidget(status_);
    right_layout->addStretch();
    top->addWidget(right);
    top->setStretchFactor(1, 1);
    split->addWidget(top);

    tabs_ = new QTabWidget;
    trace_ = new TracePanel;
    rtos_ = new RtosPanel;
    timeline_ = new TimelineView;
    timeline_->setObjectName("timeline");
    registers_ = new RegisterPanel;
    tabs_->addTab(trace_, tr("Trace"));
    tabs_->addTab(rtos_, tr("RTOS tasks"));
    tabs_->addTab(timeline_, tr("Timeline"));
    tabs_->addTab(registers_, tr("Registers"));
    split->addWidget(tabs_);
    v->addWidget(split, 1);
    setCentralWidget(central);

    fault_ = new QLabel;
    fault_->setObjectName("fault");
    fault_->setStyleSheet("color: #b00020");
    statusBar()->addWidget(fault_, 1);

    connect(reset, &QPushButton::clicked, this, [this] { select_scenario(scenario_->currentIndex()); });
    connect(scenario_, &QComboBox::activated, this, [this](int i) { select_scenario(i); });
    connect(run_, &QPushButton::toggled, this, [this](bool on) { set_running(on); });
    connect(step, &QPushButton::clicked, this, [this] { this->step(); });
    connect(run_for, &QPushButton::clicked, this, [this] {
        static const std::uint64_t amounts[] = {kMs, 10 * kMs, 100 * kMs, 1000 * kMs};
        this->run_for(amounts[amount_->currentIndex()]);
    });
    connect(run_until, &QPushButton::clicked, this,
            [this] { this->run_until(static_cast<std::uint64_t>(until_ms_->value()) * kMs); });
    connect(board_, &BoardPanel::input_changed, this, [this] { after_run(); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { refresh(); });
    timer_.setInterval(kTickMs);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });

    select_scenario(0);
}

MainWindow::~MainWindow() {
    timer_.stop();
    // Panels hold pointers into the session: detach them first.
    board_->set_session(nullptr);
    audio_->set_session(nullptr);
    session_.reset();
}

void MainWindow::select_scenario(int index) {
    set_running(false);
    board_->set_session(nullptr);
    audio_->set_session(nullptr);
    rtos_->set_session(nullptr);
    timeline_->set_session(nullptr);
    registers_->set_session(nullptr);
    trace_->set_trace(nullptr);
    glcd_->show_glcd(nullptr);
    session_.reset();  // the old one tears down first: firmware statics are shared
    session_ = std::make_unique<workbench::Session>(static_cast<std::size_t>(index));
    scenario_->setCurrentIndex(index);
    description_->setText(QString::fromStdString(session_->scenario().description));
    board_->set_session(session_.get());
    audio_->set_session(session_.get());
    rtos_->set_session(session_.get());
    timeline_->set_session(session_.get());
    registers_->set_session(session_.get());
    trace_->set_trace(&session_->board().mcu().trace());
    glcd_->show_glcd(&session_->board().glcd());
    after_run();
}

void MainWindow::run_for(std::uint64_t cycles) {
    session_->run_for(cycles);
    after_run();
}

void MainWindow::run_until(std::uint64_t cycle) {
    session_->run_until(cycle);
    after_run();
}

void MainWindow::step() {
    session_->step();
    after_run();
}

void MainWindow::set_running(bool running) {
    if (running && session_ && session_->faulted()) running = false;
    if (running) timer_.start();
    else timer_.stop();
    const QSignalBlocker block(run_);
    run_->setChecked(running);
    run_->setText(running ? tr("Pause") : tr("Run"));
}

// One timer slice: virtual time at the chosen rate, or as much as fits in about
// 15 ms of wall-clock time. The wall clock only paces the display; the simulation
// is the same whatever the pacing.
void MainWindow::tick() {
    switch (speed_->currentIndex()) {
    case 0: session_->run_for(kTickMs * kMs); break;
    case 1: session_->run_for(10 * kTickMs * kMs); break;
    default: {
        QElapsedTimer wall;
        wall.start();
        while (wall.elapsed() < 15 && !session_->faulted()) session_->run_for(5 * kMs);
    }
    }
    after_run();
}

void MainWindow::after_run() {
    if (session_->faulted()) {
        set_running(false);
        fault_->setText(tr("Stopped: %1  (Reset to start again)").arg(QString::fromStdString(session_->fault())));
    } else {
        fault_->clear();
    }
    refresh();
}

void MainWindow::refresh() {
    if (!session_) return;
    const std::uint64_t t = session_->now();
    time_->setText(tr("t = %1 cycles  (%2 ms)").arg(t).arg(static_cast<double>(t) / kMs, 0, 'f', 3));
    const auto check = session_->check();
    check_->setText((check.passed ? tr("PASS: ") : tr("check: ")) + QString::fromStdString(check.detail));
    check_->setStyleSheet(check.passed ? "color: #0a7d32" : "color: #555");
    status_->setText(QString::fromStdString(session_->status()));
    board_->refresh();
    glcd_->refresh();
    audio_->refresh();
    // The tabs are refreshed only while shown: a large trace is not re-read needlessly.
    QWidget* shown = tabs_->currentWidget();
    if (shown == trace_) trace_->refresh();
    else if (shown == rtos_) rtos_->refresh();
    else if (shown == timeline_) timeline_->refresh();
    else if (shown == registers_) registers_->refresh();
}

}  // namespace latasim::ui
