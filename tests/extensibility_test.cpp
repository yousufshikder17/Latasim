// Second-board readiness: the reusable layers work without the LPC1768 or the
// MCB1700. The RTOS kernel runs on a minimal platform with a plain tick counter,
// and the virtual USB host drives a scripted device controller. Neither test
// includes anything from lpc17xx/ or boards/.
#include "devices/usb.hpp"
#include "devices/usb_audio_host.hpp"
#include "rtos/kernel.hpp"

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

using namespace latasim;

namespace {

// A machine with nothing but a periodic tick interrupt.
class TickOnlyPlatform : public rtos::Platform {
public:
    std::uint64_t now() const override { return now_; }
    std::uint64_t cycles_to_next_event() const override { return period_ == 0 ? 0 : period_ - now_ % period_; }
    void advance(std::uint64_t cycles) override {
        now_ += cycles;
        if (period_ != 0 && now_ % period_ == 0) {
            in_isr_ = true;
            tick_();
            in_isr_ = false;
            if (done_) done_();
        }
    }
    void start_tick(std::uint32_t period, std::function<void()> tick) override {
        period_ = period;
        tick_ = std::move(tick);
    }
    void on_interrupts_done(std::function<void()> hook) override { done_ = std::move(hook); }
    bool in_interrupt() const override { return in_isr_; }
    void record(TraceEvent event) override { events.push_back(event); }

    std::vector<TraceEvent> events;

private:
    std::uint64_t now_ = 0;
    std::uint32_t period_ = 0;
    bool in_isr_ = false;
    std::function<void()> tick_, done_;
};

}  // namespace

TEST(Extensibility, RtosKernelRunsOnAnotherPlatform) {
    TickOnlyPlatform platform;
    rtos::Config c;
    c.tick_period = 1'000;
    c.tick_us = 1'000;  // a 1 ms tick on a 1 MHz part
    c.round_robin_ticks = 1;
    rtos::Kernel k(platform, c);
    std::string order;
    k.start_main([&] {
        k.initialize();
        k.create_thread([&] { for (;;) { order += 'A'; k.consume(1'000); } }, 0, "A");
        k.create_thread([&] { for (;;) { order += 'B'; k.consume(1'000); } }, 0, "B");
        k.start();
    });
    k.run_for(6'000);
    EXPECT_EQ(order.substr(0, 6), "ABABAB") << "round-robin on each 1 ms tick";
    EXPECT_EQ(k.tick_count(), 6u);
    EXPECT_FALSE(platform.events.empty()) << "RTOS events reach the platform's trace";
}

namespace {

// A device controller that plays back a fixed enumeration: descriptors for a
// mono 48 kHz speaker, ZLPs for everything else.
class ScriptedDevice : public usb::Bus {
public:
    bool connected = false;
    unsigned resets = 0, frames = 0;
    std::vector<std::vector<std::uint8_t>> iso;
    std::vector<std::array<std::uint8_t, 8>> setups;

    bool pull_up() const override { return connected; }
    void cable(bool) override {}
    void bus_reset() override { ++resets; }
    void start_of_frame() override { ++frames; }
    void setup(const std::array<std::uint8_t, 8>& p) override {
        setups.push_back(p);
        pending_.clear();
        std::vector<std::uint8_t> reply;
        if (p[1] == 6 && p[3] == 1) reply = {18, 1, 0x10, 1, 0, 0, 0, 64, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1};
        if (p[1] == 6 && p[3] == 2) reply = config_;
        const std::size_t length = p[6] | (p[7] << 8);
        if (reply.size() > length) reply.resize(length);
        for (std::size_t i = 0; i < reply.size(); i += 64)
            pending_.emplace_back(reply.begin() + static_cast<std::ptrdiff_t>(i),
                                  reply.begin() + static_cast<std::ptrdiff_t>(std::min(reply.size(), i + 64)));
        if (reply.empty() || reply.size() % 64 == 0) pending_.emplace_back();  // status or terminating ZLP
    }
    std::optional<std::vector<std::uint8_t>> in(unsigned) override {
        if (pending_.empty()) return std::vector<std::uint8_t>{};
        auto p = pending_.front();
        pending_.pop_front();
        return p;
    }
    bool control_stalled() const override { return false; }
    bool out(unsigned, const std::vector<std::uint8_t>&) override { return true; }
    void iso_out(unsigned, const std::vector<std::uint8_t>& data) override { iso.push_back(data); }

private:
    std::deque<std::vector<std::uint8_t>> pending_;
    std::vector<std::uint8_t> config_ = {
        9, 2, 52, 0, 2, 1, 0, 0x80, 50,           // configuration
        9, 4, 0, 0, 0, 1, 1, 0, 0,                // audio control
        9, 4, 1, 1, 1, 1, 2, 0, 0,                // streaming, alt 1
        11, 0x24, 2, 1, 1, 2, 16, 1, 0x80, 0xBB, 0x00,  // Type I, mono, 48000 Hz
        9, 5, 0x01, 0x09, 96, 0, 1, 0, 0,          // endpoint 1 OUT, isochronous
        5, 0, 0, 0, 0};                            // padding
};

}  // namespace

TEST(Extensibility, UsbAudioHostDrivesAnotherDeviceController) {
    ScriptedDevice device;
    usb::AudioHost pc;
    usb::TonePcm tone(48'000, 1'000, 1'000);
    pc.set_source(&tone);
    pc.frame(device);
    EXPECT_EQ(pc.state(), usb::AudioHost::State::Detached) << "nothing until the device connects";
    device.connected = true;
    for (int f = 0; f < 60; ++f) pc.frame(device);
    EXPECT_EQ(device.resets, 1u);
    ASSERT_EQ(pc.state(), usb::AudioHost::State::Streaming) << pc.state_name();
    EXPECT_EQ(pc.stream().endpoint, 1u);
    EXPECT_EQ(pc.stream().sample_rate, 48'000u);
    ASSERT_FALSE(device.iso.empty());
    EXPECT_EQ(device.iso.front().size(), 96u) << "48 samples per 1 ms frame";
    device.connected = false;
    pc.frame(device);
    EXPECT_EQ(pc.state(), usb::AudioHost::State::Detached);
}
