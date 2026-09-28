#include "ui/qt/glcd_view.hpp"

#include <QPainter>

#include <algorithm>

#include "boards/mcb1700/glcd.hpp"

namespace latasim::ui {

using mcb1700::Glcd;

GlcdView::GlcdView(QWidget* parent) : QWidget(parent), image_(Glcd::kWidth, Glcd::kHeight, QImage::Format_RGB16) {
    image_.fill(Qt::black);
    setMinimumSize(Glcd::kWidth, Glcd::kHeight);
}

void GlcdView::show_glcd(const Glcd* glcd) {
    glcd_ = glcd;
    hash_ = 0;
    image_.fill(Qt::black);
    refresh();
}

void GlcdView::set_scale(int scale) {
    scale_ = scale;
    update();
}

void GlcdView::refresh() {
    if (glcd_ == nullptr) return;
    const std::uint64_t h = glcd_->hash();
    if (h == hash_) return;
    hash_ = h;
    for (unsigned y = 0; y < Glcd::kHeight; ++y) {
        auto* line = reinterpret_cast<std::uint16_t*>(image_.scanLine(static_cast<int>(y)));
        for (unsigned x = 0; x < Glcd::kWidth; ++x) line[x] = glcd_->pixel(x, y);  // RGB565 as stored
    }
    update();
}

void GlcdView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    const int fit = std::max(1, std::min(width() / static_cast<int>(Glcd::kWidth), height() / static_cast<int>(Glcd::kHeight)));
    const int s = scale_ > 0 ? scale_ : fit;
    const QSize size(static_cast<int>(Glcd::kWidth) * s, static_cast<int>(Glcd::kHeight) * s);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(target, image_);
}

}  // namespace latasim::ui
