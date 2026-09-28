#include "ui/qt/board_panel.hpp"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "workbench/session.hpp"

namespace latasim::ui {

using mcb1700::JoystickDirection;

LedWidget::LedWidget(QWidget* parent) : QWidget(parent) { setMinimumSize(18, 18); }

void LedWidget::set_state(int state) {
    if (state == state_) return;
    state_ = state;
    update();
}

void LedWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor fill = state_ == 1 ? QColor(255, 60, 40) : state_ == 0 ? QColor(70, 20, 15) : QColor(90, 90, 90);
    p.setBrush(fill);
    p.setPen(QPen(Qt::black, 1));
    const int d = std::min(width(), height()) - 4;
    p.drawEllipse((width() - d) / 2, (height() - d) / 2, d, d);
}

BoardPanel::BoardPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* led_box = new QGroupBox(tr("LEDs"));
    auto* led_row = new QGridLayout(led_box);
    for (int i = 0; i < 8; ++i) {
        leds_[static_cast<std::size_t>(i)] = new LedWidget;
        leds_[static_cast<std::size_t>(i)]->setObjectName(QString("led%1").arg(i));
        led_row->addWidget(leds_[static_cast<std::size_t>(i)], 0, 7 - i);  // LED7 at the left, as on the board
        led_row->addWidget(new QLabel(QString::number(i)), 1, 7 - i, Qt::AlignHCenter);
    }
    layout->addWidget(led_box);

    auto* joy_box = new QGroupBox(tr("Joystick"));
    auto* joy = new QGridLayout(joy_box);
    const struct {
        JoystickDirection d;
        const char* label;
        int row, col;
    } buttons[] = {{JoystickDirection::Center, "Center", 1, 1}, {JoystickDirection::Up, "Up", 0, 1},
                   {JoystickDirection::Right, "Right", 1, 2}, {JoystickDirection::Down, "Down", 2, 1},
                   {JoystickDirection::Left, "Left", 1, 0}};
    for (const auto& b : buttons) {
        auto* button = new QPushButton(tr(b.label));
        button->setObjectName(QString("joystick_") + mcb1700::to_string(b.d));
        const int d = static_cast<int>(b.d);
        joystick_[static_cast<std::size_t>(d)] = button;
        connect(button, &QPushButton::pressed, this, [this, d] {
            if (!latch_->isChecked()) joystick(d, true);
        });
        connect(button, &QPushButton::released, this, [this, d] {
            if (!latch_->isChecked()) joystick(d, false);
        });
        connect(button, &QPushButton::clicked, this, [this, d] {
            if (!latch_->isChecked() || session_ == nullptr) return;
            joystick(d, !session_->board().is_pressed(static_cast<JoystickDirection>(d)));  // toggle
        });
        joy->addWidget(button, b.row, b.col);
    }
    latch_ = new QCheckBox(tr("Hold (click toggles)"));
    latch_->setObjectName("joystick_latch");
    joy->addWidget(latch_, 3, 0, 1, 3);
    layout->addWidget(joy_box);

    auto* int0_box = new QGroupBox(tr("INT0"));
    auto* int0_layout = new QVBoxLayout(int0_box);
    int0_ = new QPushButton(tr("INT0"));
    int0_->setObjectName("int0");
    connect(int0_, &QPushButton::pressed, this, [this] { int0(true); });
    connect(int0_, &QPushButton::released, this, [this] { int0(false); });
    int0_layout->addWidget(int0_);
    layout->addWidget(int0_box);

    auto* pot_box = new QGroupBox(tr("Potentiometer (AD0.2)"));
    auto* pot_layout = new QVBoxLayout(pot_box);
    pot_ = new QSlider(Qt::Horizontal);
    pot_->setObjectName("potentiometer");
    pot_->setRange(0, 0xFFF);
    pot_label_ = new QLabel;
    connect(pot_, &QSlider::valueChanged, this, [this](int v) {
        if (session_ == nullptr) return;
        session_->input([v](mcb1700::Board& b) { b.set_potentiometer(static_cast<std::uint32_t>(v)); });
        Q_EMIT input_changed();
    });
    pot_layout->addWidget(pot_);
    pot_layout->addWidget(pot_label_);
    layout->addWidget(pot_box);
    layout->addStretch();
}

void BoardPanel::set_session(workbench::Session* session) {
    session_ = session;
    if (session_ != nullptr) {
        const QSignalBlocker block(pot_);
        pot_->setValue(static_cast<int>(session_->board().potentiometer()));
    }
    refresh();
}

void BoardPanel::joystick(int direction, bool press) {
    if (session_ == nullptr) return;
    const auto d = static_cast<JoystickDirection>(direction);
    session_->input([d, press](mcb1700::Board& b) { press ? b.press(d) : b.release(d); });
    Q_EMIT input_changed();
}

void BoardPanel::int0(bool press) {
    if (session_ == nullptr) return;
    session_->input([press](mcb1700::Board& b) { press ? b.press_int0() : b.release_int0(); });
    Q_EMIT input_changed();
}

void BoardPanel::refresh() {
    if (session_ == nullptr) return;
    const auto& board = session_->board();
    for (unsigned i = 0; i < 8; ++i) leds_[i]->set_state(static_cast<int>(board.led(i)));
    for (unsigned d = 0; d < 5; ++d)
        joystick_[d]->setDown(board.is_pressed(static_cast<JoystickDirection>(d)));
    const std::uint32_t raw = board.potentiometer();
    pot_label_->setText(QString("raw 0x%1 (%2)  %3 V")
                            .arg(raw, 3, 16, QChar('0'))
                            .arg(raw)
                            .arg(raw * 3.3 / 4095.0, 0, 'f', 3));
}

}  // namespace latasim::ui
