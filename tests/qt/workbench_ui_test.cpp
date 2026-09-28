// Smoke tests of the Qt workbench, offscreen: the window launches, its views show
// the session's model state, its controls reach the board, and a firmware fault
// stops the session without ending the process.
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTableView>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>

#include "LPC17xx.h"
#include "media_center.h"
#include "ui/qt/board_panel.hpp"
#include "ui/qt/glcd_view.hpp"
#include "ui/qt/inspect_panels.hpp"
#include "ui/qt/main_window.hpp"
#include "ui/qt/rtos_panel.hpp"
#include "ui/qt/trace_panel.hpp"
#include "workbench/session.hpp"
#include "workbench/views.hpp"

using namespace latasim;

namespace {

constexpr std::uint64_t kMs = 100'000;

int index_of(const std::string& name) {
    const auto& all = workbench::scenarios();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].name == name) return static_cast<int>(i);
    return -1;
}

}  // namespace

class WorkbenchUi : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void launches_with_the_first_scenario() {
        ui::MainWindow w;
        w.show();
        QVERIFY(w.session() != nullptr);
        QCOMPARE(w.findChild<QComboBox*>("scenario")->count(), static_cast<int>(workbench::scenarios().size()));
        QVERIFY(w.findChild<QLabel*>("time")->text().startsWith("t = 0 cycles"));
    }

    void leds_follow_the_board() {
        ui::MainWindow w;
        w.select_scenario(index_of("Blinky_ULp"));
        w.run_for(15 * kMs);  // the chase has stepped
        for (int i = 0; i < 8; ++i) {
            auto* led = w.findChild<ui::LedWidget*>(QString("led%1").arg(i));
            QCOMPARE(led->state(), static_cast<int>(w.session()->board().led(static_cast<unsigned>(i))));
        }
    }

    void glcd_view_is_pixel_exact() {
        ui::MainWindow w;
        w.select_scenario(index_of("Media center"));
        w.run_for(60 * kMs);
        const auto& glcd = w.session()->board().glcd();
        const QImage& img = w.glcd_view()->image();
        QCOMPARE(img.format(), QImage::Format_RGB16);
        for (unsigned y = 0; y < 240; y += 7)
            for (unsigned x = 0; x < 320; x += 11)
                QCOMPARE(reinterpret_cast<const std::uint16_t*>(img.constScanLine(static_cast<int>(y)))[x], glcd.pixel(x, y));
    }

    void controls_reach_the_board() {
        ui::MainWindow w;
        w.select_scenario(index_of("Media center"));
        w.run_for(30 * kMs);
        auto* down = w.findChild<QPushButton*>("joystick_down");
        QVERIFY(down != nullptr);
        QTest::mousePress(down, Qt::LeftButton);
        QVERIFY(w.session()->board().is_pressed(mcb1700::JoystickDirection::Down));
        w.run_for(40 * kMs);
        QTest::mouseRelease(down, Qt::LeftButton);
        QVERIFY(!w.session()->board().is_pressed(mcb1700::JoystickDirection::Down));
        w.run_for(40 * kMs);
        QCOMPARE(media.selection, 1u);
        auto* int0 = w.findChild<QPushButton*>("int0");
        QTest::mousePress(int0, Qt::LeftButton);
        QVERIFY(w.session()->board().int0_pressed());
        QTest::mouseRelease(int0, Qt::LeftButton);
        w.findChild<QSlider*>("potentiometer")->setValue(0x800);
        QCOMPARE(w.session()->board().potentiometer(), 0x800u);
    }

    void trace_rtos_timeline_and_registers_show_model_state() {
        ui::MainWindow w;
        w.select_scenario(index_of("RTOS: rate-monotonic"));
        w.run_for(400 * kMs);
        w.trace_panel()->refresh();
        QVERIFY(w.trace_panel()->model().rowCount() > 0);
        w.trace_panel()->model().set_category_shown("mmio", false);
        for (int r = 0; r < w.trace_panel()->model().rowCount(); ++r)
            QVERIFY(w.trace_panel()->model().data(w.trace_panel()->model().index(r, 3), Qt::DisplayRole).toString() != "mmio");
        w.rtos_panel()->refresh();
        QCOMPARE(w.rtos_panel()->table()->rowCount(), static_cast<int>(w.session()->kernel()->threads().size()));
        w.timeline_view()->refresh();
        QVERIFY(!w.timeline_view()->data().threads.empty());
        QVERIFY(!w.timeline_view()->data().threads[3].intervals.empty());  // a task ran
        const auto trace_size = w.session()->board().mcu().trace().events().size();
        w.register_panel()->refresh();
        QCOMPARE(w.session()->board().mcu().trace().events().size(), trace_size);  // reading registers traced nothing
        const auto* stctrl = w.register_panel()->tree()->topLevelItem(1)->child(0);
        QCOMPARE(stctrl->text(0), QString("STCTRL"));
    }

    void faults_stop_the_session_not_the_application() {
        ui::MainWindow w;
        w.select_scenario(index_of("Blinky_ULp"));
        w.session()->input([](mcb1700::Board&) { latasim_mmio_read32(0x4000C000); });
        w.run_for(kMs);
        QVERIFY(w.session()->faulted());
        QVERIFY(w.findChild<QLabel*>("fault")->text().contains("bus fault"));
        w.set_running(true);
        QVERIFY(!w.running());
        w.findChild<QPushButton*>("reset")->click();
        QVERIFY(!w.session()->faulted());
    }

    void running_advances_virtual_time() {
        ui::MainWindow w;
        w.select_scenario(index_of("RTOS: delays"));
        w.set_running(true);
        QTest::qWait(120);
        w.set_running(false);
        QVERIFY(w.session()->now() > 0);
        w.step();
        QVERIFY(w.findChild<QLabel*>("check") != nullptr);
    }
};

QTEST_MAIN(WorkbenchUi)
#include "workbench_ui_test.moc"
