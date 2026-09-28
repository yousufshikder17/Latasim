#include "ui/qt/rtos_panel.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

#include "workbench/session.hpp"

namespace latasim::ui {
namespace {

const char* priority_name(int level) {
    static const char* const names[] = {"(idle demon)", "Idle", "Low", "BelowNormal", "Normal", "AboveNormal", "High",
                                        "Realtime"};
    return level >= 0 && level < 8 ? names[level] : "(boosted)";
}

QColor thread_colour(unsigned id) {
    if (id == 0) return QColor(200, 200, 200);
    static const QColor palette[] = {QColor(66, 133, 244), QColor(219, 68, 55),  QColor(244, 180, 0),
                                     QColor(15, 157, 88),  QColor(171, 71, 188), QColor(0, 172, 193),
                                     QColor(255, 112, 67), QColor(158, 157, 36)};
    return palette[(id - 1) % 8];
}

}  // namespace

RtosPanel::RtosPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    summary_ = new QLabel;
    summary_->setObjectName("rtos_summary");
    layout->addWidget(summary_);
    table_ = new QTableWidget(0, 8);
    table_->setObjectName("rtos_table");
    table_->setHorizontalHeaderLabels(
        {"#", "thread", "state", "base priority", "effective priority", "waiting for", "wake tick", "CPU"});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);
}

void RtosPanel::set_session(const workbench::Session* session) {
    session_ = session;
    refresh();
}

void RtosPanel::refresh() {
    const rtos::Kernel* k = session_ != nullptr ? session_->kernel() : nullptr;
    if (k == nullptr) {
        table_->setRowCount(0);
        summary_->setText(tr("This scenario runs without an RTOS."));
        return;
    }
    const auto threads = k->threads();
    const double total = static_cast<double>(std::max<std::uint64_t>(1, session_->now()));
    table_->setRowCount(static_cast<int>(threads.size()));
    for (std::size_t r = 0; r < threads.size(); ++r) {
        const auto& t = threads[r];
        QString waiting;
        if (t.state == rtos::ThreadState::WaitingSignal) waiting = QString("signals 0x%1").arg(t.waiting_for, 4, 16, QChar('0'));
        else if (t.state == rtos::ThreadState::WaitingMutex) waiting = QString("mutex M%1").arg(t.waiting_mutex);
        else if (t.state == rtos::ThreadState::WaitingMessage) waiting = "timer message";
        else if (t.state == rtos::ThreadState::Delayed) waiting = "delay";
        const QString cells[] = {QString::number(t.id),
                                 QString::fromStdString(t.name),
                                 rtos::to_string(t.state),
                                 priority_name(t.base_level),
                                 priority_name(t.level),
                                 waiting,
                                 t.wake_tick ? QString::number(*t.wake_tick) : QString(),
                                 QString("%1 %").arg(100.0 * static_cast<double>(t.run_cycles) / total, 0, 'f', 1)};
        const QColor background = t.state == rtos::ThreadState::Running ? QColor(200, 240, 200)
                                  : t.state == rtos::ThreadState::Ready  ? QColor(255, 250, 205)
                                  : t.state == rtos::ThreadState::Terminated ? QColor(230, 230, 230)
                                                                             : QColor(255, 225, 225);
        for (int c = 0; c < 8; ++c) {
            auto* item = table_->item(static_cast<int>(r), c);
            if (item == nullptr) table_->setItem(static_cast<int>(r), c, item = new QTableWidgetItem);
            item->setText(cells[c]);
            item->setBackground(background);
        }
    }
    summary_->setText(tr("Tick %1  |  idle %2 %  |  kernel-call time %3 cycles  |  running: %4")
                          .arg(k->tick_count())
                          .arg(100.0 * static_cast<double>(k->idle_cycles()) / total, 0, 'f', 1)
                          .arg(k->call_cycles_total())
                          .arg(QString::fromStdString(threads.at(k->running_thread()).name)));
}

// ---- timeline ----

TimelineView::TimelineView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(160);
    setToolTip(tr("Wheel: zoom.  Drag: pan.  Double-click: follow the latest time."));
}

void TimelineView::set_session(const workbench::Session* session) {
    session_ = session;
    width_ = 0;
    refresh();
}

void TimelineView::set_window(std::uint64_t start, std::uint64_t width) {
    start_ = start;
    width_ = width;
    update();
}

void TimelineView::refresh() {
    if (session_ != nullptr) data_ = workbench::timeline(*session_);
    update();
}

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    const int label_w = 120, row_h = 18, top = 4;
    const std::uint64_t end = data_.end;
    const std::uint64_t width = width_ != 0 ? width_ : span_;
    const std::uint64_t start = width_ != 0 ? start_ : (end > width ? end - width : 0);
    const double px = static_cast<double>(std::max(1, this->width() - label_w)) / static_cast<double>(width);
    auto x_of = [&](std::uint64_t c) {
        return label_w + static_cast<int>((static_cast<double>(c) - static_cast<double>(start)) * px);
    };
    int row = 0;
    auto draw_row = [&](const QString& name, const QColor& colour, const std::vector<rtos::Interval>* intervals,
                        const std::vector<workbench::TimelineMarker>& markers) {
        const int y = top + row * row_h;
        p.setPen(Qt::black);
        p.drawText(4, y, label_w - 8, row_h, Qt::AlignVCenter, name);
        p.setPen(QColor(235, 235, 235));
        p.drawLine(label_w, y + row_h - 1, this->width(), y + row_h - 1);
        if (intervals != nullptr)
            for (const auto& i : *intervals) {
                if (i.end < start || i.start > start + width) continue;
                const int x0 = std::max(label_w, x_of(i.start)), x1 = std::max(x0 + 1, x_of(i.end));
                p.fillRect(x0, y + 3, x1 - x0, row_h - 6, colour);
            }
        p.setPen(Qt::darkGray);
        for (const auto& m : markers) {
            if (m.cycles < start || m.cycles > start + width) continue;
            const int x = x_of(m.cycles);
            p.drawLine(x, y + 1, x, y + row_h - 2);
        }
        ++row;
    };
    for (const auto& t : data_.threads)
        draw_row(QString::fromStdString(t.name), thread_colour(t.thread), &t.intervals, t.markers);
    draw_row(tr("interrupts"), Qt::black, nullptr, data_.interrupts);
    // Time axis.
    const int y = top + row * row_h + 4;
    p.setPen(Qt::black);
    p.drawText(label_w, y, 200, 16, Qt::AlignLeft, QString("%1 ms").arg(static_cast<double>(start) / 100'000.0, 0, 'f', 3));
    p.drawText(this->width() - 204, y, 200, 16, Qt::AlignRight,
               QString("%1 ms").arg(static_cast<double>(start + width) / 100'000.0, 0, 'f', 3));
}

void TimelineView::wheelEvent(QWheelEvent* event) {
    const std::uint64_t width = width_ != 0 ? width_ : span_;
    const std::uint64_t start = width_ != 0 ? start_ : (data_.end > width ? data_.end - width : 0);
    const std::uint64_t next = event->angleDelta().y() > 0 ? std::max<std::uint64_t>(1'000, width / 2) : width * 2;
    set_window(start + (width > next ? (width - next) / 2 : 0), next);
}

void TimelineView::mousePressEvent(QMouseEvent* event) {
    drag_x_ = event->position().toPoint().x();
    if (width_ == 0) {  // freeze the followed window before panning
        start_ = data_.end > span_ ? data_.end - span_ : 0;
        width_ = span_;
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* event) {
    const int x = event->position().toPoint().x();
    const double per_px = static_cast<double>(width_) / std::max(1, width() - 120);
    const auto shift = static_cast<std::int64_t>((drag_x_ - x) * per_px);
    drag_x_ = x;
    const auto s = static_cast<std::int64_t>(start_) + shift;
    set_window(static_cast<std::uint64_t>(std::max<std::int64_t>(0, s)), width_);
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent*) { set_window(0, 0); }

}  // namespace latasim::ui
