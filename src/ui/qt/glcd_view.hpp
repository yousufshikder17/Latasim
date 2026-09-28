#pragma once
// The GLCD framebuffer, pixel for pixel: the model's RGB565 GRAM copied into a
// QImage of the same format (no colour conversion), scaled by whole multiples
// with nearest-neighbour sampling so no pixel is blended. Painting only reads.
#include <QImage>
#include <QWidget>

#include <cstdint>

namespace latasim::mcb1700 {
class Glcd;
}

namespace latasim::ui {

class GlcdView : public QWidget {
public:
    explicit GlcdView(QWidget* parent = nullptr);
    void show_glcd(const mcb1700::Glcd* glcd);  // nullptr: blank
    void refresh();                             // re-copy if the framebuffer changed
    // 0 = the largest whole multiple that fits; otherwise a fixed scale.
    void set_scale(int scale);
    const QImage& image() const { return image_; }
    QSize sizeHint() const override { return {640, 480}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const mcb1700::Glcd* glcd_ = nullptr;
    QImage image_;
    std::uint64_t hash_ = 0;
    int scale_ = 0;
};

}  // namespace latasim::ui
