#pragma once
// The board's controls and indicators: LEDs, joystick, INT0, potentiometer. It
// holds no board state: LEDs are read from the board on refresh(), and every
// control calls the session's board inputs.
#include <QWidget>

#include <array>

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

namespace latasim::workbench {
class Session;
}

namespace latasim::ui {

class LedWidget : public QWidget {
    Q_OBJECT
public:
    explicit LedWidget(QWidget* parent = nullptr);
    void set_state(int state);  // 0 off, 1 on, 2 undriven
    int state() const { return state_; }
    QSize sizeHint() const override { return {22, 22}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int state_ = 2;
};

class BoardPanel : public QWidget {
    Q_OBJECT
public:
    explicit BoardPanel(QWidget* parent = nullptr);
    void set_session(workbench::Session* session);
    void refresh();

Q_SIGNALS:
    void input_changed();  // after an input, which can run firmware

private:
    void joystick(int direction, bool press);
    void int0(bool press);

    workbench::Session* session_ = nullptr;
    std::array<LedWidget*, 8> leds_{};
    std::array<QPushButton*, 5> joystick_{};
    QPushButton* int0_ = nullptr;
    QCheckBox* latch_ = nullptr;
    QSlider* pot_ = nullptr;
    QLabel* pot_label_ = nullptr;
};

}  // namespace latasim::ui
