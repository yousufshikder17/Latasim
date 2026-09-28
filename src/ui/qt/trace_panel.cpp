#include "ui/qt/trace_panel.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
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
    endResetModel();
    refresh();
}

void TraceModel::refresh() {
    if (trace_ == nullptr) return;
    const auto& events = trace_->events();
    if (events.size() < seen_) return rebuild();  // a new session's trace
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
    if (at_end) view_->scrollToBottom();  // follow new events unless scrolled back
}

}  // namespace latasim::ui
