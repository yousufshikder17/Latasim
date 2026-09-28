#pragma once
// The hardware trace as a table: sequence, time, category, operation, subject,
// value (from trace::describe, never from formatted lines). Category filters
// choose which events are listed; filtering and scrolling only read the trace.
#include <QAbstractTableModel>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

#include "trace/trace.hpp"

class QTableView;

namespace latasim::ui {

class TraceModel : public QAbstractTableModel {
public:
    void set_trace(const Trace* trace);
    // Picks up events recorded since the last call.
    void refresh();
    void set_category_shown(const std::string& category, bool shown);
    bool category_shown(const std::string& category) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void rebuild();

    const Trace* trace_ = nullptr;
    std::size_t seen_ = 0;
    std::vector<std::size_t> rows_;  // indices into the trace
    std::map<std::string, bool> hidden_;
};

class TracePanel : public QWidget {
public:
    explicit TracePanel(QWidget* parent = nullptr);
    void set_trace(const Trace* trace);
    void refresh();
    TraceModel& model() { return model_; }

private:
    TraceModel model_;
    QTableView* view_ = nullptr;
};

}  // namespace latasim::ui
