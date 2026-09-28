// latasim-workbench: the desktop front end (docs/phase5/desktop.md).
#include <QApplication>

#include "ui/qt/main_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    latasim::ui::MainWindow window;
    window.resize(1400, 900);
    window.show();
    return QApplication::exec();
}
