#include "ui/qt/trace_panel.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QTableView>
#include <QVBoxLayout>

namespace latasim::ui {
namespace {

const char* const kCategories[] = {"mmio", "interrupt", "timer", "adc", "glcd", "input", "led", "rtos", "usb/audio"};
constexpr int kColumns = 7;

}  // namespace

void TraceModel::set_trace(const Trace* trace) {
    trace_ = trace;
    rebuild();
}

void TraceModel::set_category_shown(const std::string& category, bool shown) {
    hidden_[category] = !shown;
    rebuild();
}

bool TraceModel::category_shown(const std::string& category) const {
    const auto it = hidden_.find(category);
    return it == hidden_.end() || !it->second;
}

void TraceModel::rebuild() {
    beginResetModel();
    rows_.clear();
    seen_ = 0;
    dropped_seen_ = trace_ != nullptr ? trace_->dropped() : 0;
    endResetModel();
    refresh();
}

void TraceModel::refresh() {
    if (trace_ == nullptr) return;
    const auto& events = trace_->events();
    // A new session's trace, or old accesses dropped: the indices have moved.
    if (events.size() < seen_ || trace_->dropped() != dropped_seen_) return rebuild();
    std::vector<std::size_t> added;
    for (std::size_t i = seen_; i < events.size(); ++i)
        if (category_shown(describe(events[i]).category)) added.push_back(i);
    seen_ = events.size();
    if (added.empty()) return;
    const int first = static_cast<int>(rows_.size());
    beginInsertRows({}, first, first + static_cast<int>(added.size()) - 1);
    rows_.insert(rows_.end(), added.begin(), added.end());
    endInsertRows();
}

int TraceModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
int TraceModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : kColumns; }

QVariant TraceModel::data(const QModelIndex& index, int role) const {
    if (role != Qt::DisplayRole || trace_ == nullptr || index.row() >= static_cast<int>(rows_.size())) return {};
    const TraceEvent& e = trace_->events()[rows_[static_cast<std::size_t>(index.row())]];
    const TraceParts parts = describe(e);
    switch (index.column()) {
    case 0: return QVariant::fromValue<qulonglong>(e.seq);
    case 1: return QVariant::fromValue<qulonglong>(e.cycles);
    case 2: return QString::number(static_cast<double>(e.cycles) / 100'000.0, 'f', 5);
    case 3: return QString::fromStdString(parts.category);
    case 4: return QString::fromStdString(parts.operation);
    case 5: return QString::fromStdString(parts.subject);
    case 6: return QString::fromStdString(parts.value);
    default: return {};
    }
}

QVariant TraceModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* const names[kColumns] = {"#", "cycles", "ms", "category", "operation", "subject", "value"};
    return section < kColumns ? QString(names[section]) : QVariant{};
}

TracePanel::TracePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* filters = new QHBoxLayout;
    for (const char* c : kCategories) {
        auto* box = new QCheckBox(c);
        box->setObjectName(QString("trace_filter_") + c);
        box->setChecked(true);
        const std::string category = c;
        connect(box, &QCheckBox::toggled, this, [this, category](bool on) { model_.set_category_shown(category, on); });
        filters->addWidget(box);
    }
    filters->addStretch();
    dropped_ = new QLabel;
    dropped_->setObjectName("trace_dropped");
    filters->addWidget(dropped_);
    layout->addLayout(filters);
    view_ = new QTableView;
    view_->setObjectName("trace_table");
    view_->setModel(&model_);
    view_->verticalHeader()->setVisible(false);
    view_->verticalHeader()->setDefaultSectionSize(18);
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(view_);
}

void TracePanel::set_trace(const Trace* trace) { model_.set_trace(trace); }

void TracePanel::refresh() {
    const bool at_end = view_->verticalScrollBar()->value() == view_->verticalScrollBar()->maximum();
    model_.refresh();
    const Trace* t = model_.trace();
    dropped_->setText(t != nullptr && t->dropped() != 0
                          ? tr("%1 oldest MMIO accesses not kept (limit %2)").arg(t->dropped()).arg(t->mmio_capacity())
                          : QString());
    if (at_end) view_->scrollToBottom();  // follow new events unless scrolled back
}

}  // namespace latasim::ui
