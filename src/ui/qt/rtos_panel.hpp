#pragma once
// RTOS views: the task table (like an RTOS-aware debugger's task list) and the
// scheduler timeline (workbench::timeline). Both read the kernel's inspection API.
#include <QWidget>

#include <cstdint>

#include "workbench/views.hpp"

class QLabel;
class QTableWidget;

namespace latasim::workbench {
class Session;
}

namespace latasim::ui {

class RtosPanel : public QWidget {
public:
    explicit RtosPanel(QWidget* parent = nullptr);
    void set_session(const workbench::Session* session);
    void refresh();
    QTableWidget* table() { return table_; }

private:
    const workbench::Session* session_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;
};

class TimelineView : public QWidget {
public:
    explicit TimelineView(QWidget* parent = nullptr);
    void set_session(const workbench::Session* session);
    void refresh();
    // Visible window, in cycles; a width of 0 follows the latest `span` cycles.
    void set_window(std::uint64_t start, std::uint64_t width);
    const workbench::Timeline& data() const { return data_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    const workbench::Session* session_ = nullptr;
    workbench::Timeline data_;
    std::uint64_t start_ = 0;
    std::uint64_t width_ = 0;         // 0: follow the end
    std::uint64_t span_ = 20'000'000;  // 200 ms when following
    int drag_x_ = 0;
};

}  // namespace latasim::ui
