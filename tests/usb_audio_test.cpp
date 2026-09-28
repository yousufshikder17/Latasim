// USB audio end to end: the representative speaker firmware on Keil's USB device
// and DAC drivers (unchanged), a virtual PC enumerating it and streaming PCM, and
// the board's speaker recording what the DAC plays.
#include "usb_speaker.h"

#include "boards/mcb1700/board.hpp"
#include "devices/usb_audio_host.hpp"
#include "host/binding.hpp"
#include "lpc17xx/nvic.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace latasim;

namespace {

constexpr std::uint64_t kMs = 100'000;

struct Speaker {
    mcb1700::Board board;
    host::FirmwareBinding bind{board};
    usb::AudioHost pc;
    usb::TonePcm tone{USB_SPEAKER_RATE, 1000, 8000};
    Speaker() {
        usb_speaker_reset();
        board.mcu().bind_handler(latasim::lpc17xx::kUsbIrq, USB_IRQHandler);
        board.mcu().bind_handler(latasim::lpc17xx::kTimer0Irq, usb_speaker_timer_irq);
        pc.set_source(&tone);
        usb_speaker_start();
        board.connect_usb_host(&pc);
    }
    // Keil's USB driver keeps "initialized" and "powered" in statics, which outlive
    // the board on the host; stopping resets them for the next test.
    ~Speaker() { usb_speaker_stop(); }
    void run(std::uint64_t cycles) { board.mcu().advance_cycles(cycles); }
};

}  // namespace

TEST(UsbAudio, HostEnumeratesTheSpeakerAndStreams) {
    Speaker s;
    s.run(60 * kMs);
    EXPECT_EQ(s.pc.state(), usb::AudioHost::State::Streaming) << s.pc.state_name();
    const usb::AudioStream& st = s.pc.stream();
    EXPECT_EQ(st.interface, 1u);
    EXPECT_EQ(st.alternate, 1u);
    EXPECT_EQ(st.endpoint, 3u);
    EXPECT_EQ(st.max_packet, 64u);
    EXPECT_EQ(st.sample_rate, 32'000u);
    EXPECT_EQ(st.channels, 1u);
    EXPECT_TRUE(usb_speaker.configured);
    EXPECT_TRUE(usb_speaker.streaming);
    EXPECT_EQ(s.board.mcu().usb().address(), 1u);
    EXPECT_GT(usb_speaker.packets, 0u);
    EXPECT_EQ(usb_speaker.samples_received, usb_speaker.packets * 32) << "32 samples per 1 ms packet";
}

TEST(UsbAudio, PlaybackStartsAtHalfBufferAndKeepsUp) {
    Speaker s;
    s.run(200 * kMs);
    EXPECT_TRUE(usb_speaker.playing);
    EXPECT_EQ(usb_speaker.underruns, 0u) << "the timer and the frames run at the same rate";
    EXPECT_EQ(usb_speaker.overruns, 0u);
    // Once playing, the level stays near the start threshold.
    EXPECT_NEAR(static_cast<double>(usb_speaker.buffer_level), USB_SPEAKER_BUFFER / 2.0, 40.0);
    // The speaker's first value is DAC_Initialize's DACR = 0; then one per sample
    // played, 3125 cycles apart (32 kHz).
    const auto& sp = s.board.speaker();
    EXPECT_EQ(sp.size(), usb_speaker.samples_played + 1);
    EXPECT_EQ(sp.front().value, 0u);
    ASSERT_GT(sp.size(), 100u);
    for (std::size_t i = 2; i < 100; ++i) EXPECT_EQ(sp[i].cycles - sp[i - 1].cycles, 3125u) << i;
}

// Sample order and scaling: the DAC plays the host's samples in order, scaled by
// the example's formula at the firmware's volume.
TEST(UsbAudio, DacPlaysTheHostSamplesInOrderAtTheVolume) {
    Speaker s;
    usb_speaker_set_volume(256);
    s.run(200 * kMs);
    usb::TonePcm reference(USB_SPEAKER_RATE, 1000, 8000);
    const auto& sp = s.board.speaker();
    ASSERT_GT(sp.size(), 1000u);
    for (std::size_t i = 1; i <= 1000; ++i) {  // [0] is DAC_Initialize's DACR = 0
        const auto sample = static_cast<std::uint16_t>(reference.next());
        const std::uint32_t scaled = ((0x8000u + sample) & 0xFFFFu) * 256u >> 8;
        EXPECT_EQ(sp[i].value, (scaled >> 6) & 0x3FFu) << i;
    }
}

TEST(UsbAudio, VolumeScalesTheOutput) {
    auto peak = [](std::uint32_t volume) {
        Speaker s;
        usb_speaker_set_volume(volume);
        s.run(200 * kMs);
        std::uint16_t hi = 0, lo = 1023;
        const auto& sp = s.board.speaker();
        for (std::size_t i = 1; i < sp.size(); ++i) {
            const auto& p = sp[i];
            hi = std::max(hi, p.value);
            lo = std::min(lo, p.value);
        }
        return hi - lo;
    };
    const int full = peak(256), half = peak(128), quiet = peak(16);
    EXPECT_NEAR(half, full / 2, 2);
    EXPECT_NEAR(quiet, full / 16, 2);
}

TEST(UsbAudio, PausingTheHostUnderrunsAndStopsPlayback) {
    Speaker s;
    s.run(200 * kMs);
    s.pc.set_playing(false);
    s.run(100 * kMs);
    EXPECT_FALSE(usb_speaker.playing);
    EXPECT_EQ(usb_speaker.underruns, 1u) << "one empty tick stops the timer";
    EXPECT_EQ(usb_speaker.buffer_level, 0u);
    s.pc.set_playing(true);
    s.run(100 * kMs);
    EXPECT_TRUE(usb_speaker.playing) << "restarts at half a buffer";
}

TEST(UsbAudio, StoppingDisconnectsFromTheHost) {
    Speaker s;
    s.run(100 * kMs);
    usb_speaker_stop();
    s.run(5 * kMs);
    EXPECT_EQ(s.pc.state(), usb::AudioHost::State::Detached);
    EXPECT_FALSE(s.board.mcu().usb().pull_up());
    EXPECT_FALSE(usb_speaker.streaming);
    const auto played = s.board.speaker().size();
    s.run(50 * kMs);
    EXPECT_EQ(s.board.speaker().size(), played) << "silent once stopped";
    // Starting again reconnects, and the host enumerates it again.
    usb_speaker_start();
    s.run(100 * kMs);
    EXPECT_EQ(s.pc.state(), usb::AudioHost::State::Streaming);
    EXPECT_EQ(s.pc.enumerations(), 2u);
}

TEST(UsbAudio, RepeatedRunsGiveIdenticalOutput) {
    auto run = [] {
        Speaker s;
        s.run(150 * kMs);
        return std::pair{s.board.speaker(), s.board.mcu().trace().events().size()};
    };
    const auto first = run();
    const auto second = run();
    EXPECT_EQ(first.first, second.first);
    EXPECT_EQ(first.second, second.second);
}
